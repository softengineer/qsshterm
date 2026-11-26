#include "qsshterm.h"
#include "qssh_helpers.h"
#include "qssh_forward.h"
#include <libssh/server.h>   // ssh_send_keepalive
#include "qssh_theme.h"
#include <QFont>
#include <QFontDatabase>

QSSHTerm::QSSHTerm(const SiteInfo &info, QWidget *parent)
    : QTermWidget(0, parent), siteInfo(info)
{
  SiteTree::resolveJumpHosts(siteInfo);
  connect(ThemeManager::instance(), &ThemeManager::changed, this, [this]() {
    if (siteInfo.colorScheme.isEmpty())
      setColorScheme(ThemeManager::instance()->effectiveTerminalScheme());
  });
  // bidi shaping lays out every line through QTextLayout: very slow on bulk output
  this->setBidiEnabled(false);
  applyTerminalSettings();
}

void QSSHTerm::applyTerminalSettings()
{
  setColorScheme(siteInfo.colorScheme.isEmpty() ? ThemeManager::instance()->effectiveTerminalScheme()
                                                : siteInfo.colorScheme);
  if (siteInfo.encoding == QLatin1String("Latin1"))
    setTextCodec(QStringConverter::Latin1);
  else if (siteInfo.encoding == QLatin1String("System"))
    setTextCodec(QStringConverter::System);
  else
    setTextCodec(QStringConverter::Utf8);
  if (siteInfo.scrollback != -2)
    setHistorySize(siteInfo.scrollback);
  if (!siteInfo.keyboardLayout.isEmpty())
    setKeyBindings(siteInfo.keyboardLayout);
  changeFont();
}

QSSHTerm::~QSSHTerm()
{
  if (session) {
    // The session lives in its own thread: let it shut down there. Waiting
    // here could block the GUI on a slow connect/disconnect.
    QObject::disconnect(session, nullptr, this, nullptr);
    QMetaObject::invokeMethod(session, &QSSHSession::close, Qt::QueuedConnection);
  }
}

void QSSHTerm::changeFont() {
  QFont font = QApplication::font();
#if defined(Q_OS_MACOS)
  font.setFamily("Monaco");
#elif defined(Q_OS_WIN)
  font.setFamily("Courier New");
#else
  font.setFamily(QFontDatabase::systemFont(QFontDatabase::FixedFont).family());
#endif
  if (!siteInfo.fontFamily.isEmpty())
    font.setFamily(siteInfo.fontFamily);
  font.setStyleHint(QFont::TypeWriter);
  font.setPointSize(siteInfo.fontSize > 0 ? siteInfo.fontSize : 12);

  this->setTerminalFont(font);
}

void QSSHTerm::readData(const char *d, int size) {
    if (sessionState == DISCONNECT) {
        // the session is gone: Enter reconnects, everything else is dropped
        if (!reconnectPending && QByteArrayView(d, size).contains('\r')) {
            reconnectPending = true;
            emit reconnect();
        }
        return;
    }
    emit sendByteArray(QByteArray(d, size));
}

void QSSHTerm::writeData(const QByteArray &data) {
    // don't let error messages printed after a disconnect turn the tab "busy"
    if (sessionState != DISCONNECT)
        this->sendChangeIcon();
    this->receivedRemoteData(data);
}

void QSSHTerm::sendChangeIcon() {
  this->sessionState = BUSY;
  emit icon_change(this->termKey, BUSY);
}

void QSSHTerm::sessionError(int) {
  reconnectPending = false;
  connected = false;
  cipherInfo.clear();
  this->sessionState = DISCONNECT;
  emit icon_change(this->termKey, DISCONNECT);
  emit statusChanged();
}

void QSSHTerm::sessionConnected() {
  reconnectPending = false;
  connected = true;
  this->sessionState = IDLE;
  emit icon_change(this->termKey, IDLE);
  emit statusChanged();
}

void QSSHTerm::resizeEvent(QResizeEvent *event) {
  QTermWidget::resizeEvent(event);
  emit resizeSshWindow(this->screenColumnsCount(), this->screenLinesCount());
}

void QSSHTerm::start() {
    if (session)
        return;

    //set cursor to blink, default is static cursor
    this->setBlinkingCursor(true);
    this->setTerminalSizeHint(true);

    session = new QSSHSession(siteInfo, screenColumnsCount(), screenLinesCount());
    // no parent: the thread must outlive this widget until the session has closed
    QThread *thread = new QThread();
    session->moveToThread(thread);

    connect(this, &QTermWidget::sendData, this, &QSSHTerm::readData);
    connect(this, &QSSHTerm::sendByteArray, session, &QSSHSession::readByteArray);

    connect(thread, &QThread::started, session, &QSSHSession::process);
    connect(session, &QSSHSession::finished, thread, &QThread::quit);
    connect(session, &QSSHSession::finished, session, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    connect(this, &QSSHTerm::resizeSshWindow, session, &QSSHSession::resizeEvent);
    connect(this, &QTermWidget::copyAvailable, this, &QSSHTerm::copySelect);

    connect(this, &QSSHTerm::connect_to, session, &QSSHSession::connect_to);
    connect(this, &QSSHTerm::reconnect, session, &QSSHSession::reconnect);
    connect(this, &QSSHTerm::disconnectSession, session, &QSSHSession::disconnect);
    connect(this, &QSSHTerm::reset, session, &QSSHSession::reset);

    connect(session, &QSSHSession::dispatchData, this, &QSSHTerm::writeData);
    connect(session, &QSSHSession::sessionError, this, &QSSHTerm::sessionError);
    connect(session, &QSSHSession::sessionConnected, this, &QSSHTerm::sessionConnected);
    connect(session, &QSSHSession::sessionInfo, this, [this](const QString &info) {
        cipherInfo = info;
        emit statusChanged();
    });
    connect(session, &QSSHSession::passwordEntered, this, [this](const QString &password, bool save) {
        siteInfo.password = password;   // in memory only: lets SFTP and Duplicate skip the prompt
        if (save)
            emit savePasswordRequested(siteInfo.sitename, password);
    });

    this->startTerminalTeletype();
    thread->start();
}

void QSSHTerm::copySelect(bool available)
{
  // copy-on-select; copyAvailable(false) is emitted when the selection is cleared
  if (available)
    copyClipboard();
}

QSSHSession::QSSHSession(const SiteInfo &info, int columns, int lines)
    : siteInfo(info), columns(columns), lines(lines)
{
}

QSSHSession::~QSSHSession()
{
  disconnect();
}

void QSSHSession::emitText(const QString &text) {
  emit dispatchData(text.toUtf8());
}

void QSSHSession::fail(const QString &message) {
  qDebug() << "ssh session error:" << message;
  emit sessionError(1);
  emitText(QStringLiteral("\r\n%1\r\nPress enter to reconnect ...").arg(message));
  disconnect();
}

void QSSHSession::resizeEvent(int width, int height) {
    if (width <= 1 || height <= 1)
      return;

    columns = width;
    lines = height;
    if (channel)
        ssh_channel_change_pty_size(channel, columns, lines);
}

void QSSHSession::readByteArray(const QByteArray &array) {
  if (!channel)
    return;
  ssh_channel_write(channel, array.constData(), array.size());
  // libssh may have read (and buffered) incoming data while writing
  scheduleRead();
}

void QSSHSession::process() {
  connect_to();
}

void QSSHSession::scheduleRead() {
  if (wakePending)
    return;
  wakePending = true;
  QTimer::singleShot(0, this, [this]() {
    wakePending = false;
    select_loop();
  });
}

bool QSSHSession::readChannel() {
    // drain everything available into one chunk: one cross-thread signal and
    // one terminal update per wakeup instead of one per 4 KB read
    QByteArray data;
    int nbytes = 0;
    for (int is_stderr = 0; is_stderr < 2 && channel; ++is_stderr) {
      do {
        nbytes = ssh_channel_read_nonblocking(channel, buffer, sizeof(buffer), is_stderr);
        if (nbytes > 0)
            data.append(buffer, nbytes);
      } while (nbytes > 0 && data.size() < 1024 * 1024);
      if (nbytes == SSH_ERROR)
        break;
    }

    if (!data.isEmpty())
        emit dispatchData(data);
    if (nbytes == SSH_ERROR)
        sshStateCheck();
    return !data.isEmpty();
}

void QSSHSession::select_loop() {
    // reading any channel processes packets for all of them: go round until
    // nothing moves so no data is left sitting in libssh's buffers
    for (int round = 0; round < 64 && channel; ++round) {
        bool moved = readChannel();
        if (forwards && channel)
            moved |= forwards->pump();
        if (!moved)
            break;
    }
}

void QSSHSession::sshStateCheck() {
  if (!session || !channel)
    return;

  int status = ssh_get_status(session);
  // SSH_WRITE_PENDING is not an error, just ignore it
  status &= ~SSH_WRITE_PENDING;
  if (status != 0 || ssh_channel_is_eof(channel) || ssh_channel_is_closed(channel)) {
    uint32_t exitCode = 0;
    int exitStatus = -1;
    if ((ssh_channel_is_eof(channel) || ssh_channel_is_closed(channel))
        && ssh_channel_get_exit_state(channel, &exitCode, nullptr, nullptr) == SSH_OK)
      exitStatus = int(exitCode);
    fail(exitStatus >= 0 && !siteInfo.remoteCommand.trimmed().isEmpty()
             ? QStringLiteral("Command exited with status %1.").arg(exitStatus)
             : QStringLiteral("SSH Session is broken."));
  }
}

void QSSHSession::sendKeepAlive() {
  if (session && ssh_send_keepalive(session) != SSH_OK)
    fail(QStringLiteral("Connection lost (keepalive failed): %1").arg(QString::fromUtf8(ssh_get_error(session))));
}

void QSSHSession::sendStartupCommands() {
  if (!channel)
    return;
  const QStringList commands = siteInfo.startupCommands.split('\n');
  for (QString line : commands) {
    if (line.endsWith('\r'))
      line.chop(1);
    if (line.trimmed().isEmpty())
      continue;
    const QByteArray bytes = line.toUtf8() + '\r';
    ssh_channel_write(channel, bytes.constData(), uint32_t(bytes.size()));
  }
  scheduleRead();
}

bool QSSHSession::shell() {
    channel = ssh_channel_new(session);
    if (!channel)
        return false;

    if (ssh_channel_open_session(channel) != SSH_OK)
        return false;

    const QByteArray term = (siteInfo.termType.trimmed().isEmpty() ? QStringLiteral("xterm")
                                                                   : siteInfo.termType.trimmed()).toUtf8();
    if (ssh_channel_request_pty_size(channel, term.constData(), columns, lines) != SSH_OK)
        return false;

    QStringList refused;
    for (const auto &var : std::as_const(siteInfo.environment)) {
        if (var.first.trimmed().isEmpty())
            continue;
        if (ssh_channel_request_env(channel, var.first.trimmed().toUtf8().constData(),
                                    var.second.toUtf8().constData()) != SSH_OK)
            refused << var.first.trimmed();
    }
    if (!refused.isEmpty())
        emitText(QStringLiteral("Server refused environment variables %1 (see AcceptEnv in sshd_config)\r\n")
                     .arg(refused.join(", ")));

    forwards->setupShellChannel(channel);

    const QString command = siteInfo.remoteCommand.trimmed();
    if (command.isEmpty() ? ssh_channel_request_shell(channel) != SSH_OK
                          : ssh_channel_request_exec(channel, command.toUtf8().constData()) != SSH_OK)
        return false;

    read_notifier = new QSocketNotifier(ssh_get_fd(session), QSocketNotifier::Read, this);
    // Qt6 changed activated(int) to activated(QSocketDescriptor, Type); the old
    // string based SIGNAL(activated(int)) connection silently failed.
    connect(read_notifier, &QSocketNotifier::activated, this, &QSSHSession::select_loop);
    read_notifier->setEnabled(true);

    timer = new QTimer(this);
    timer->setSingleShot(false);
    timer->setInterval(1000);
    connect(timer, &QTimer::timeout, this, &QSSHSession::sshStateCheck);
    connect(timer, &QTimer::timeout, this, &QSSHSession::select_loop);
    timer->start();

    if (siteInfo.keepAliveInterval > 0) {
        keepAlive = new QTimer(this);
        keepAlive->setInterval(siteInfo.keepAliveInterval * 1000);
        connect(keepAlive, &QTimer::timeout, this, &QSSHSession::sendKeepAlive);
        keepAlive->start();
    }

    forwards->start();

    // give the shell a moment to print its prompt before typing into it
    if (!siteInfo.startupCommands.trimmed().isEmpty())
        QTimer::singleShot(500, this, &QSSHSession::sendStartupCommands);

    // pick up anything that arrived before the notifier existed
    select_loop();
    return true;
}

void QSSHSession::reconnect() {
   connect_to();
}

void QSSHSession::disconnect() {
    // may run from a slot of the notifier/timer itself: never delete them directly
    if (read_notifier) {
      read_notifier->setEnabled(false);
      read_notifier->deleteLater();
      read_notifier = nullptr;
    }
    for (QTimer **t : {&timer, &keepAlive}) {
      if (*t) {
        (*t)->stop();
        (*t)->deleteLater();
        *t = nullptr;
      }
    }
    // forwarded channels first: they belong to the session freed below
    if (forwards)
      forwards->stop();
    if (channel) {
      ssh_channel_close(channel);
      ssh_channel_free(channel);
      channel = nullptr;
    }
    if (session) {
      connection->close();
      session = nullptr;
      qDebug() << "SSH session is closed";
    }
}

void QSSHSession::reset() {
    reconnect();
}

void QSSHSession::close() {
    disconnect();
    emit finished();
}

void QSSHSession::connect_to() {
    disconnect();

    if (!connection) {
        connection = std::make_unique<qssh::Connection>(siteInfo);
        connection->offerSavePassword = !siteInfo.sitename.isEmpty();
        connection->noDelay = true;
        connection->notice = [this](const QString &text) {
            QString t = text;
            emitText(t.replace(QLatin1String("\n"), QLatin1String("\r\n")) + QStringLiteral("\r\n"));
        };
        forwards = new qssh::ForwardManager(connection.get(), siteInfo, this);
        connect(forwards, &qssh::ForwardManager::notice, this, [this](const QString &text) {
            emitText(text + QStringLiteral("\r\n"));
        });
        connect(forwards, &qssh::ForwardManager::wake, this, &QSSHSession::scheduleRead);
        forwards->install();
    }

    emitText(QStringLiteral("Connecting to host ...\r\n"));
    QString error;
    if (!connection->open(&error)) {
        fail(error);
        return;
    }
    session = connection->session();
    if (connection->passwordTyped)
        emit passwordEntered(connection->site().password, connection->savePasswordRequested);

    if (!shell()) {
        fail(QStringLiteral("Unable to open a shell: %1").arg(QString::fromUtf8(ssh_get_error(session))));
        return;
    }

    const char *cipher = ssh_get_cipher_out(session);
    emit sessionInfo(QStringLiteral("ssh2: %1").arg(QString::fromUtf8(cipher ? cipher : "?")));
    emit sessionConnected();
}

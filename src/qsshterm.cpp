#include "qsshterm.h"
#include "qssh_helpers.h"
#include <QFont>
#include <QFontDatabase>

QSSHTerm::QSSHTerm(const SiteInfo &info, QWidget *parent)
    : QTermWidget(0, parent), siteInfo(info)
{
  this->setColorScheme("Ubuntu");
  // bidi shaping lays out every line through QTextLayout: very slow on bulk output
  this->setBidiEnabled(false);
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
  font.setStyleHint(QFont::TypeWriter);
  font.setPointSize(12);

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
  this->sessionState = DISCONNECT;
  emit icon_change(this->termKey, DISCONNECT);
}

void QSSHTerm::sessionConnected() {
  reconnectPending = false;
  this->sessionState = IDLE;
  emit icon_change(this->termKey, IDLE);
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
  if (channel)
    ssh_channel_write(channel, array.constData(), array.size());
}

void QSSHSession::process() {
  connect_to();
}

void QSSHSession::select_loop() {
    if (!channel)
        return;

    // drain everything available into one chunk: one cross-thread signal and
    // one terminal update per wakeup instead of one per 4 KB read
    QByteArray data;
    int nbytes = 0;
    do {
      nbytes = ssh_channel_read_nonblocking(channel, buffer, sizeof(buffer), 0);
      if (nbytes > 0)
          data.append(buffer, nbytes);
    } while (nbytes > 0 && data.size() < 1024 * 1024);

    if (!data.isEmpty())
        emit dispatchData(data);

    if (nbytes == SSH_ERROR)
        sshStateCheck();
}

void QSSHSession::sshStateCheck() {
  if (!session || !channel)
    return;

  int status = ssh_get_status(session);
  // SSH_WRITE_PENDING is not an error, just ignore it
  status &= ~SSH_WRITE_PENDING;
  if (status != 0 || ssh_channel_is_eof(channel) || ssh_channel_is_closed(channel)) {
    qDebug() << "There is network issue";
    fail(QStringLiteral("SSH Session is broken."));
  }
}

bool QSSHSession::shell() {
    channel = ssh_channel_new(session);
    if (!channel)
        return false;

    if (ssh_channel_open_session(channel) != SSH_OK)
        return false;

    if (ssh_channel_request_pty_size(channel, "xterm", columns, lines) != SSH_OK)
        return false;

    if (ssh_channel_request_shell(channel) != SSH_OK)
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
    timer->start();

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
    if (timer) {
      timer->stop();
      timer->deleteLater();
      timer = nullptr;
    }
    if (channel) {
      ssh_channel_close(channel);
      ssh_channel_free(channel);
      channel = nullptr;
    }
    if (session) {
      ssh_disconnect(session);
      ssh_free(session);
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

    emitText(QStringLiteral("Connecting to host ...\r\n"));
    session = ssh_new();
    if (!session) {
        fail(QStringLiteral("Unable to create an SSH session."));
        return;
    }

    int verbosity = SSH_LOG_WARNING;
    qssh::applySiteOptions(session, siteInfo);
    ssh_options_set(session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);

    if (ssh_connect(session) != SSH_OK) {
        fail(QStringLiteral("Fail to connect to server: %1").arg(QString::fromUtf8(ssh_get_error(session))));
        return;
    }

    QString hostMessage;
    if (!qssh::verifyKnownHost(session, &hostMessage)) {
        fail(hostMessage);
        return;
    }
    if (!hostMessage.isEmpty())
        emitText(hostMessage + QStringLiteral("\r\n"));

    if (qssh::authenticate(session, siteInfo) != SSH_AUTH_SUCCESS) {
        fail(QStringLiteral("Authentication failed: %1").arg(QString::fromUtf8(ssh_get_error(session))));
        return;
    }

    char *banner = ssh_get_issue_banner(session);
    if (banner) {
        emitText(QString::fromUtf8(banner));
        ssh_string_free_char(banner);
    }

    if (!shell()) {
        fail(QStringLiteral("Unable to open a shell: %1").arg(QString::fromUtf8(ssh_get_error(session))));
        return;
    }

    emit sessionConnected();
}

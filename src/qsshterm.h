#ifndef WIDGET_H
#define WIDGET_H
#include <libssh/libssh.h>
#include <qtermwidget6/qtermwidget.h>
#include <QtWidgets>
#include <QByteArray>
#include <QThread>
#include <QTimer>
#include <QSocketNotifier>
#include "qsiteconfig.h"
#include "qsshterm_common.h"
#include <memory>

namespace qssh { class Connection; class ForwardManager; }

class QSSHSession;

class QSSHTerm : public QTermWidget
{
    Q_OBJECT
public:
    explicit QSSHTerm(const SiteInfo &info, QWidget *parent = nullptr);
    ~QSSHTerm() override;

    void setTabTermKey(const QString &termKey) {
        this->termKey = termKey;
    }

    const QString &getTermKey() const {
        return this->termKey;
    }
    void start();
    void changeFont();

    SSHTermState state() const {
        return sessionState;
    }

    void setState(SSHTermState state) {
        this->sessionState = state;
    }
    bool isConnected() const { return connected; }
    /** e.g. "ssh2: aes256-gcm@openssh.com", empty while not connected */
    const QString &connectionInfo() const { return cipherInfo; }

    SiteInfo siteInfo;

public slots:
    /** Keyboard input from the terminal widget, forwarded to the SSH channel. */
    void readData(const char *data, int size);
    /** Output received from the SSH channel, shown in the terminal widget. */
    void writeData(const QByteArray &data);
    void sessionError(int code);
    void sessionConnected();
    /** Applies the font, colours and other per-site terminal settings. */
    void applyTerminalSettings();

signals:
    /** The user typed the password at the prompt and ticked "Save password". */
    void savePasswordRequested(QString siteName, QString password);
    void icon_change(QString termKey, SSHTermState state);
    void reconnect();

    /**
      open ssh session, connect to host
    */
    void connect_to();
    void disconnectSession();
    void reset();
    void sendByteArray(const QByteArray &array);
    void resizeSshWindow(int columns, int lines);
    /** connected / disconnected / cipher changed */
    void statusChanged();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    QSSHSession *session = nullptr;
    QString termKey;
    SSHTermState sessionState = IDLE;
    bool reconnectPending = false;
    bool connected = false;
    QString cipherInfo;
    void sendChangeIcon();

private slots:
    void copySelect(bool available);
};

/**
 * Owns the libssh session. Lives in its own thread; it never touches the
 * QSSHTerm widget directly, everything goes through queued signals.
 */
class QSSHSession : public QObject {
    Q_OBJECT

public:
    QSSHSession(const SiteInfo &info, int columns, int lines);
    ~QSSHSession() override;

public slots:
    void process();
    void readByteArray(const QByteArray &array);
    void resizeEvent(int columns, int lines);

    void reconnect();

    /**
      open ssh session, connect to host
    */
    void connect_to();
    void disconnect();
    void reset();
    /** Disconnects and asks the owning thread to finish; the object deletes itself. */
    void close();

signals:
    void finished();
    void dispatchData(const QByteArray &data);
    void sessionError(int);
    void sessionConnected();
    void sessionInfo(QString info);
    /** Password typed at the prompt, kept for SFTP and reconnects. */
    void passwordEntered(QString password, bool save);

private slots:
    void select_loop();
    void sshStateCheck();
    void sendKeepAlive();
    void sendStartupCommands();

private:
    SiteInfo siteInfo;
    int columns;
    int lines;

    std::unique_ptr<qssh::Connection> connection;
    qssh::ForwardManager *forwards = nullptr;
    ssh_session session = nullptr;
    ssh_channel channel = nullptr;
    char buffer[64 * 1024];
    QTimer *timer = nullptr;
    QTimer *keepAlive = nullptr;
    QSocketNotifier *read_notifier = nullptr;
    bool wakePending = false;

    bool shell();
    bool readChannel();
    void scheduleRead();
    void fail(const QString &message);
    void emitText(const QString &text);
};

#endif // WIDGET_H

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
    SiteInfo siteInfo;

public slots:
    /** Keyboard input from the terminal widget, forwarded to the SSH channel. */
    void readData(const char *data, int size);
    /** Output received from the SSH channel, shown in the terminal widget. */
    void writeData(const QByteArray &data);
    void sessionError(int code);
    void sessionConnected();

signals:
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

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    QSSHSession *session = nullptr;
    QString termKey;
    SSHTermState sessionState = IDLE;
    bool reconnectPending = false;
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

private slots:
    void select_loop();
    void sshStateCheck();

private:
    SiteInfo siteInfo;
    int columns;
    int lines;

    ssh_session session = nullptr;
    ssh_channel channel = nullptr;
    char buffer[4096];
    QTimer *timer = nullptr;
    QSocketNotifier *read_notifier = nullptr;

    bool shell();
    void fail(const QString &message);
    void emitText(const QString &text);
};

#endif // WIDGET_H

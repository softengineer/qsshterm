#ifndef WIDGET_H
#define WIDGET_H
#include <libssh/libssh.h>
#include <libssh/callbacks.h>
#include <qtermwidget5/qtermwidget.h>
#include <QApplication>
#include <QGuiApplication> 
#include <QtWidgets>
#include <QDebug>
#include <QTimer>
#include <QBuffer>
#include <QThread>
#include <QTcpSocket>
#include <fstream>
#include <QTimer>
#include <QSocketNotifier>
#include "qsiteconfig.h"
#include "qsshterm_common.h"

class QSSHSession;
class qsshTabTerm;


class QSSHTerm : public QTermWidget
{
    Q_OBJECT
public:
    QSSHTerm(SiteInfo info, QWidget * parent = 0);
    void setTabTermKey(const QString termKey) {
        this->termKey = termKey;
    }

    QString & getTermKey() {
        return this->termKey;
    }
    void start();
    void changeFont();
    SiteInfo siteInfo;

public slots:
	void readData(const char *data, int size);
    void writeData(const char *data, int size);
    void sessionError(int j);

signals:
    void icon_change(QString termKey, SSHTermState state);
    void reconnect();
    void sendData(const char *, int);

    /**
      open ssh session, connect to host
    */
    void connect_to();
    void disconnect();
    void reset();
	void sendByteArray(const QByteArray array);
    void resizeSshWindow(const int width, const int height);

    void rightMouseButtonPressed();


protected:
    void resizeEvent(QResizeEvent * event) {
        emit resizeSshWindow(this->screenColumnsCount(), this->screenLinesCount());
    }
private:
	QSSHSession *session;
    QString termKey;
    void sendChangeIcon();


private slots:
    void copySelect();
    void mouseEvent();
};

class QSSHSession :public QObject {
	Q_OBJECT

	public:

	Q_ENUMS(SessionState)
    enum State
    {
        StateClosed,
        StateClosing,
        StateInit,
        StateConnecting,
        StateIsKnown,
        StateUnknownHost,
        StateAuthChoose,
        StateAuthContinue,
        StateAuthNone,
        StateAuthAutoPubkey,
        StateAuthPassword,
        StateAuthNeedPassword,
        StateAuthKbi,
        StateAuthKbiQuestions,
        StateAuthAllFailed,
        StateOpened,
        StateError
    };

    QSSHSession(QObject *parent = 0, QSSHTerm * term = 0);
	void deinit();

    /**
    * try to connect host again
    */
   
public slots:
    void process();
    void readByteArray(const QByteArray array);
    void log(QString msg);
    void resizeEvent(const int width, const int height);

    void reconnect();

    /**
      open ssh session, connect to host
    */
    void connect_to();
    void disconnect();
    void reset();

signals:
    void finished();
    void error(QString err);
    void dispatchData(const char *data, int size);
    void sessionError(int);

private slots:
    int select_loop();
    void sshStateCheck();


private :
	ssh_session session;
	ssh_channel channel;
    char buffer[1024];
	QSSHTerm *qterm;
    QTimer    *timer;

    QSocketNotifier * read_notifier;
    QSocketNotifier * error_notifier;
	void shell(ssh_session session);
	void error(ssh_session session);
	int authenticate_kbdint(ssh_session session, const char *password);
	int authenticate_console(ssh_session session);

};

#endif // WIDGET_H

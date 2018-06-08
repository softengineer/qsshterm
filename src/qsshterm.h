#ifndef WIDGET_H
#define WIDGET_H
#include <libssh/libssh.h>
#include <libssh/callbacks.h>
#include <qtermwidget5/qtermwidget.h>
#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QTimer>
#include <QBuffer>
#include <QThread>
#include <QTcpSocket>
#include <fstream>
#include <QTimer>
#include <QSocketNotifier>
#include "qsiteconfig.h"

class QSSHSession;


class QSSHTerm : public QTermWidget
{
    Q_OBJECT

public:
    QSSHTerm(SiteInfo info, QWidget * parent = 0);
    void setTabIndex(const int idx) {
        this->index = idx;
    }
    void start();
    SiteInfo siteInfo;

public slots:
	void readData(const char *data, int size);

	signals:
	void sendByteArray(const QByteArray array);
    void resizeSshWindow(const int width, const int height);


protected:
    void resizeEvent(QResizeEvent * event) {
        emit resizeSshWindow(this->screenColumnsCount(), this->screenLinesCount());
    }
private:
	QSSHSession *session;
    int index;
    
private slots:
    void copySelect();
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
    void reconnect();

    /**
      open ssh session, connect to host
    */
    void connect_to();
    void disconnect();
    void reset();

public slots:
    void process();
    void readByteArray(const QByteArray array);
    void log(QString msg);
    void resizeEvent(const int width, const int height);

signals:
    void finished();
    void error(QString err);
    void sendData(const char *data, int size);

private slots:
    int select_loop(int );


private :
	ssh_session session;
	ssh_channel channel;
    char buffer[1024];
	QSSHTerm *qterm;
	QTimer * timer;
    QSocketNotifier * read_notifier;

	void shell(ssh_session session);
	void error(ssh_session session);
	int authenticate_kbdint(ssh_session session, const char *password);
	int authenticate_console(ssh_session session);

};

#endif // WIDGET_H

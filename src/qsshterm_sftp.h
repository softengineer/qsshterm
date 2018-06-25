#ifndef SSHTERM_SFTP_H
#define SSHTERM_SFTP_H

#include <libssh/libssh.h>
#include <libssh/sftp.h>
#include <libssh/callbacks.h>
#include <QtWidgets>

#include "qsshterm_common.h"

class QSSHTerm_Sftp_Session;


class QSSHTerm_Sftp_Dialog : public QDialog
{
    Q_OBJECT

public:
    QSSHTerm_Sftp_Dialog (QWidget *parent = 0) {
    	qDebug() << "create diagloue";
    	initGui();
    };
    ~QSSHTerm_Sftp_Dialog(){};

    void setSiteInfo(SiteInfo  info) {
    	siteInfo = info;
    }

    signals:
    void notifyNewSession();

private :
	SiteInfo  siteInfo;
    void initGui();
    void initLocalHome();
    QSSHTerm_Sftp_Session * session;

};

enum FileInfo
{
	Folder,
	File,
	Symbolink
};

struct FileStruct {
	QString name;
	long long unsigned int size;
	int permission;
	FileInfo info;
};

class QSSHTerm_Sftp_Session : QObject
{
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
	QSSHTerm_Sftp_Session(SiteInfo & info, QObject * parent = 0):siteInfo(info) {

	}

	~QSSHTerm_Sftp_Session(){

	};

	void process();

signals:
	void resp_lscurdir(QByteArray response);
	void resp_state(int state);
	void resp_chdir(QString curDir);
	void resp_rmdir(bool status);
	void resp_rename(bool status);
	void resp_remove(bool status);
	void resp_refresh(bool status);
	void resp_download(QString file, float percentage);
	void resp_upload(QString file, float percentage);
	void resp_pwd(QString dir);
	void msg_send(QString msg);
	void dispatchData(const char * data, int len);

public slots:
	void connect_to();
	void lscurdir();
	void close();
	void reconnect_to();
	void chdir(QString dir);
	void rmdir(QString dir);
	void remove(QString file);
	void rename(QString file, QString newfile);
	void download(QString file, QString localDir);
	void upload(QString localFile, QString dir);
	void pwd();

private :
	ssh_session session;
	sftp_session sftp;
	SiteInfo & siteInfo;

	int authenticate_console(ssh_session);
	void error(ssh_session session);
};

#endif
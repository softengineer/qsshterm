#include "qsshterm_sftp.h"

#include <QTextEdit>
#include <QSplitter>
#include <QVBoxLayout>


void QSSHTerm_Sftp_Dialog::initGui() 
{
  this->setWindowTitle(QString::fromUtf8("SFTP Session"));
  QListWidget *localFileList = new QListWidget();
  QListWidget *remoteFileList = new QListWidget();


  QComboBox *leftPathBox = new QComboBox;
  QLineEdit *leftPath = new QLineEdit;

  QComboBox *rightPathBox = new QComboBox;
  QLineEdit *rightPath = new QLineEdit;

  QWidget * leftWidget = new QWidget();
  QWidget * rightWidget = new QWidget();
  QWidget * bottomWidget = new QWidget();

  QTextEdit *console = new QTextEdit();

  QSplitter *horizonSplitter = new QSplitter;
  QSplitter *verticalSplitter = new QSplitter;

  QVBoxLayout *layout = new QVBoxLayout(this);
  QVBoxLayout *leftlayout = new QVBoxLayout(leftWidget);
  QVBoxLayout *rightlayout = new QVBoxLayout(rightWidget);
  QVBoxLayout *bottmlayout = new QVBoxLayout(bottomWidget);

  leftlayout->addWidget(leftPathBox);
  leftlayout->addWidget(leftPath);
  leftlayout->addWidget(localFileList);

  rightlayout->addWidget(rightPathBox);
  rightlayout->addWidget(rightPath);
  rightlayout->addWidget(remoteFileList);
  bottmlayout->addWidget(console);

  QWidget *container = new QWidget;
  QVBoxLayout *container_layout = new QVBoxLayout;

  horizonSplitter->addWidget(leftWidget);
  horizonSplitter->addWidget(rightWidget);

  container_layout->addWidget(horizonSplitter);
  container->setLayout(container_layout);

  verticalSplitter->setOrientation(Qt::Vertical);
  verticalSplitter->addWidget(container);
  verticalSplitter->addWidget(bottomWidget);

  layout->addWidget(verticalSplitter);

  initLocalHome();
}

void QSSHTerm_Sftp_Dialog::initLocalHome() 
{
  QString homeDir = QDir::homePath();
}

void QSSHTerm_Sftp_Session::connect_to()
{
	  qDebug() << "ssh is initializing";
    ssh_threads_set_callbacks(ssh_threads_get_pthread());
    ssh_init();
    session = ssh_new();
    int verbosity = 1;
    if (!siteInfo.username.isNull())
      	ssh_options_set(session, SSH_OPTIONS_USER, siteInfo.username.toLatin1().data());
    ssh_options_set(session, SSH_OPTIONS_HOST, siteInfo.hostname.toLatin1().data());
    ssh_options_set(session, SSH_OPTIONS_PORT, &siteInfo.port);
    int rc = ssh_connect(session);

    if (rc != SSH_OK) {
        qDebug() << "Connection to host failed!";
        return;
    }

    int known = ssh_is_server_known(session);
    int state= 0; 
    if (state != 0)
       return ;

    char * banner=ssh_get_issue_banner(session);
    if(banner) {
         qDebug()<<banner;
         free(banner);
    }

    authenticate_console(session);
	 sftp = sftp_new(session);
	 sftp_dir dir = sftp_opendir(sftp,"~");
}

int QSSHTerm_Sftp_Session::authenticate_console(ssh_session session){
  int rc;
  int method;
  char password[128] = {0};
  char *banner;

  // Try to authenticate
  rc = ssh_userauth_none(session, NULL);
  if (rc == SSH_AUTH_ERROR) {
    error(session);
    return rc;
  }

  method = ssh_userauth_list(session, NULL);
  qDebug() << "debug method" << method;
  while (rc != SSH_AUTH_SUCCESS) {
    if (method & SSH_AUTH_METHOD_GSSAPI_MIC){
        rc = ssh_userauth_gssapi(session);
        if(rc == SSH_AUTH_ERROR) {
            error(session);
            return rc;
        } else if (rc == SSH_AUTH_SUCCESS) {
            break;
        }
    }
    // Try to authenticate with public key first
    if (!siteInfo.privateKeyPath.isNull()) {
      if (method & SSH_AUTH_METHOD_PUBLICKEY) {
        ssh_key privkey;
        if(ssh_pki_import_privkey_file(siteInfo.privateKeyPath.toLatin1().data(),NULL,
                                             NULL,NULL,&privkey) == SSH_OK)
        {
            rc = ssh_userauth_publickey(session, NULL, privkey);
        }
        ssh_key_free(privkey);
        //rc = ssh_userauth_publickey_auto(session, NULL, NULL);
        if (rc == SSH_AUTH_ERROR) {
            error(session);
            return rc;
        } else if (rc == SSH_AUTH_SUCCESS) {
          break;
        }
      }
    }
    // Try to authenticate with keyboard interactive";
 
    char *passwd = "Password: ";
    emit dispatchData(passwd, strlen(passwd));

    // Try to authenticate with password
    if (method & SSH_AUTH_METHOD_PASSWORD) {
      if (!siteInfo.password.isNull()) {

        rc = ssh_userauth_password(session, NULL, siteInfo.password.toLatin1().data());
      }
      if (rc == SSH_AUTH_ERROR) {
        error(session);
        return rc;
      } else if (rc == SSH_AUTH_SUCCESS) {
        break;
      }
    }
    memset(password, 0, sizeof(password));
  }

  banner = ssh_get_issue_banner(session);
  if (banner) {
    emit dispatchData(banner, strlen(banner));
    ssh_string_free_char(banner);
  }

  return rc;
}



void QSSHTerm_Sftp_Session::process() {

}

void QSSHTerm_Sftp_Session::close(){

}

void QSSHTerm_Sftp_Session::reconnect_to(){

}

void QSSHTerm_Sftp_Session::chdir(QString dir){

}


void QSSHTerm_Sftp_Session::lscurdir(){

}

void QSSHTerm_Sftp_Session::rmdir(QString dir){

}

void QSSHTerm_Sftp_Session::remove(QString file){

}

void QSSHTerm_Sftp_Session::rename(QString file, QString newfile){

}

void QSSHTerm_Sftp_Session::download(QString file, QString localDir){

}

void QSSHTerm_Sftp_Session::upload(QString localFile, QString dir){

}

void QSSHTerm_Sftp_Session::pwd(){

}

void QSSHTerm_Sftp_Session::error(ssh_session session){
  fprintf(stderr,"Authentication failed: %s\n",ssh_get_error(session));
}
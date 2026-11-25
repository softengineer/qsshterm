#include "qsshterm_sftp.h"
#include "qssh_helpers.h"

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
  // TODO: populate the local file list
}

QSSHTerm_Sftp_Session::~QSSHTerm_Sftp_Session()
{
    close();
}

void QSSHTerm_Sftp_Session::connect_to()
{
    close();
    session = ssh_new();
    if (!session)
        return;

    int verbosity = SSH_LOG_WARNING;
    qssh::applySiteOptions(session, siteInfo);
    ssh_options_set(session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);

    if (ssh_connect(session) != SSH_OK) {
        emit msg_send(QStringLiteral("Connection to host failed: %1").arg(QString::fromUtf8(ssh_get_error(session))));
        close();
        return;
    }

    QString hostMessage;
    const bool trusted = qssh::verifyKnownHost(session, &hostMessage);
    if (!hostMessage.isEmpty())
        emit msg_send(hostMessage);
    if (!trusted) {
        close();
        return;
    }

    if (qssh::authenticate(session, siteInfo) != SSH_AUTH_SUCCESS) {
        emit msg_send(QStringLiteral("Authentication failed: %1").arg(QString::fromUtf8(ssh_get_error(session))));
        close();
        return;
    }

    sftp = sftp_new(session);
    if (!sftp || sftp_init(sftp) != SSH_OK) {
        emit msg_send(QStringLiteral("Unable to start SFTP: %1").arg(QString::fromUtf8(ssh_get_error(session))));
        close();
        return;
    }
}

void QSSHTerm_Sftp_Session::process() {

}

void QSSHTerm_Sftp_Session::close(){
    if (sftp) {
        sftp_free(sftp);
        sftp = nullptr;
    }
    if (session) {
        ssh_disconnect(session);
        ssh_free(session);
        session = nullptr;
    }
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

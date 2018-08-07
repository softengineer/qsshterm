#include "qsshterm.h"
#include <unistd.h>
#include <QFont>

QSSHTerm::QSSHTerm(SiteInfo info, QWidget *parent)
    : QTermWidget(0,parent), termKey(QString()), siteInfo(info), sessionState(IDLE)
{  
  this->setColorScheme("Ubuntu");
  changeFont();
}

void QSSHTerm::changeFont() {
  QFont font = QApplication::font();
  #ifdef Q_WS_MAC
      font.setFamily("Monaco");
  #elif defined(Q_WS_QWS)
      font.setFamily("fixed");
  #elif defined(Q_WS_WIN)
      font.setFamily("Courier New");
      qDebug() << "using window font";
  #else
      //font.setFamily("Monospace");
      font.setFamily("Courier New");
  #endif
    font.setPointSize(12);

  this->setTerminalFont(font);
  qDebug() << this->getTerminalFont();
}

void QSSHTerm::readData(const char * d, int size) {
    QByteArray data(d, size);
    emit sendByteArray(data);
}

void QSSHTerm::writeData(const char * d, int size) {
        this->sendChangeIcon();
        QByteArray data(d, size);
        this->receivedRemoteData(data);
}

void QSSHTerm::sendChangeIcon() {
  this->sessionState = BUSY;
  emit icon_change(this->termKey, BUSY);
}

void QSSHTerm::sessionError(int j){
       qDebug() << "session trigger exception";
       emit icon_change(this->termKey, DISCONNECT);
       this->sessionState = DISCONNECT;
}


void QSSHTerm::start() {
     session = new QSSHSession(NULL, this);

     //set cursor to blink, default is static cursor
     this->setBlinkingCursor(true);
   // connect(tester, SIGNAL(error(QAbstractSocket::SocketError)),this,SLOT(atError()));

    // Here we start an empty pty.
    QThread *thread = new QThread();
    session->moveToThread(thread);
    connect(this, &QSSHTerm::sendData,this, &QSSHTerm::readData);
   
    connect(this, &QSSHTerm::sendByteArray,session, &QSSHSession::readByteArray, Qt::QueuedConnection);

    connect(thread, SIGNAL(started()), session, SLOT(process()));

    connect(session, SIGNAL(finished()), thread, SLOT(quit()));
    connect(session, SIGNAL(finished()), session, SLOT(deleteLater()));
    connect(thread, SIGNAL(finished()), thread, SLOT(deleteLater()));
    connect(this, &QSSHTerm::resizeSshWindow, session, &QSSHSession::resizeEvent);

    connect(this, SIGNAL(copyAvailable(bool)), this, SLOT(copySelect( )));

    connect(this, SIGNAL(connect_to()), session, SLOT(connect_to()));
    connect(this, SIGNAL(reconnect()), session, SLOT(reconnect()));
    connect(this, SIGNAL(disconnect()), session, SLOT(disconnect()));
    connect(this, SIGNAL(reset()), session, SLOT(reset())); 
    connect(session, SIGNAL(sessionError(int)), this, SLOT(sessionError(int)), Qt::QueuedConnection);
   // connect(this, SIGNAL(rightMouseButtonPressed()), this, SLOT(mouseEvent()));
   
    this->startTerminalTeletype();
    thread->start();
}

void QSSHTerm::mouseEvent() {
 
}

void QSSHTerm::copySelect() 
{
  copyClipboard();
}

QSSHSession::QSSHSession( QObject *parent , QSSHTerm * term)
    : qterm(term), session(0), channel(0)
{
}

void QSSHSession::resizeEvent(const int width, const int height) {
    qterm->changeFont();
    int wwidth = qterm->screenColumnsCount();
    int wheight = qterm->screenLinesCount();
    if (width == 1 || height == 1) {
      return;
    }
    
    if (this->channel) {
        ssh_channel_change_pty_size(channel,wwidth, wheight);
    }
}

void QSSHSession::readByteArray(const QByteArray array) {
  int num = ssh_channel_write(this->channel, array.data(), array.size());
}

void QSSHSession::error(ssh_session session){
  fprintf(stderr,"Authentication failed: %s\n",ssh_get_error(session));
}

void QSSHSession::process() {
  // Read anything from remote terminal via socket and show it on widget.
  connect(this,&QSSHSession::dispatchData, qterm, &QSSHTerm::writeData, Qt::BlockingQueuedConnection);
  connect_to();
}

int QSSHSession::authenticate_kbdint(ssh_session session, const char *password) {
    int err;

    err = ssh_userauth_kbdint(session, NULL, NULL);
    while (err == SSH_AUTH_INFO) {
        const char *instruction;
        const char *name;
        char buffer[128];
        int i, n;

        name = ssh_userauth_kbdint_getname(session);
        instruction = ssh_userauth_kbdint_getinstruction(session);
        n = ssh_userauth_kbdint_getnprompts(session);

        if (name && strlen(name) > 0) {
            printf("%s\n", name);
        }

        if (instruction && strlen(instruction) > 0) {
            printf("%s\n", instruction);
        }

        for (i = 0; i < n; i++) {
            const char *answer;
            const char *prompt;
            char echo;

            prompt = ssh_userauth_kbdint_getprompt(session, i, &echo);
            if (prompt == NULL) {
                break;
            }

            if (echo) {
                char *p;

                printf("%s", prompt);

                if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
                    return SSH_AUTH_ERROR;
                }

                buffer[sizeof(buffer) - 1] = '\0';
                if ((p = strchr(buffer, '\n'))) {
                    *p = '\0';
                }

                if (ssh_userauth_kbdint_setanswer(session, i, buffer) < 0) {
                    return SSH_AUTH_ERROR;
                }

                memset(buffer, 0, strlen(buffer));
            } else {
                if (password && strstr(prompt, "Password:")) {
                    answer = password;
                } else {
                    buffer[0] = '\0';

                    if (ssh_getpass(prompt, buffer, sizeof(buffer), 0, 0) < 0) {
                        return SSH_AUTH_ERROR;
                    }
                    answer = buffer;
                }
                err = ssh_userauth_kbdint_setanswer(session, i, answer);
                memset(buffer, 0, sizeof(buffer));
                if (err < 0) {
                    return SSH_AUTH_ERROR;
                }
            }
        }
        err=ssh_userauth_kbdint(session,NULL,NULL);
    }

    return err;
}

int QSSHSession::authenticate_console(ssh_session session){
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
    if (!qterm->siteInfo.privateKeyPath.isNull()) {
      if (method & SSH_AUTH_METHOD_PUBLICKEY) {
        ssh_key privkey;
        if(ssh_pki_import_privkey_file(qterm->siteInfo.privateKeyPath.toLatin1().data(),NULL,
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
    if (method & SSH_AUTH_METHOD_INTERACTIVE) {
      rc = authenticate_kbdint(session, NULL);
      if (rc == SSH_AUTH_ERROR) {
        error(session);
        return rc;
      } else if (rc == SSH_AUTH_SUCCESS) {
        break;
      }
    }

    char *passwd = "Password: \r\n";
    emit dispatchData(passwd, strlen(passwd));

    // Try to authenticate with password
    if (method & SSH_AUTH_METHOD_PASSWORD) {
      if (!qterm->siteInfo.password.isNull()) {

        rc = ssh_userauth_password(session, NULL, qterm->siteInfo.password.toLatin1().data());
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

void QSSHSession::log(QString msg) {
     std::ofstream fout("/home/davidfan/Work/sshterminal/qlibssh/log.txt", std::ofstream::out | std::ofstream::app);
     fout << msg.toLatin1().data() << "\n";
     fout << flush;
     fout.close();
}

int QSSHSession::select_loop(){
    int nbytes = 0;
    do {
      nbytes = ssh_channel_read_nonblocking(channel, buffer, sizeof(buffer), 0);
      if (nbytes < 0) return SSH_ERROR;
      if (nbytes > 0) {
          emit dispatchData(buffer, nbytes);
          QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
      }
      
    } while(nbytes > 0);
}

void QSSHSession::sshStateCheck() {
  
  bool iseof = ssh_channel_is_eof(channel);
  int status = ssh_get_status(session);
  if (status == SSH_WRITE_PENDING) {
      // this is not an error, just ignore it
      status = 0;
  }
  if (status != 0 || iseof) {
     qDebug() << "There is network issue" ;
    emit sessionError(1);
    timer->stop();
    char * msg = "\r\nSSH Session is broken, press enter to restart ...";
    emit dispatchData(msg, strlen(msg));
  }
}

void QSSHSession::shell(ssh_session session){
    this->channel = ssh_channel_new(session);
    ssh_channel_accept_x11(channel, 10);
    if(ssh_channel_open_session(channel)){
        printf("error opening channel : %s\n",ssh_get_error(session));
        return;
    }
  
    ssh_channel_request_pty(channel);
    if(ssh_channel_request_shell(channel)){
        printf("Requesting shell : %s\n",ssh_get_error(session));
        return;
    }
    qterm->setTerminalSizeHint(true);
    int width = qterm->screenColumnsCount();
    int height = qterm->screenLinesCount();
    qDebug() << "open session :"<<width <<"," << height;
    ssh_channel_change_pty_size(channel,width, height);

    socket_t socket = ssh_get_fd(session);
    //qDebug() << LIBSSH_DAVIDFAN;
   
    read_notifier = new QSocketNotifier(socket,
                                         QSocketNotifier::Read,
                                         this);
    read_notifier->setEnabled(true);
    connect(read_notifier, SIGNAL(activated(int)),
            this, SLOT(select_loop()));

    // error_notifier = new QSocketNotifier(socket,
    //                                      QSocketNotifier::Exception,
    //                                      this);
   
    // connect(error_notifier, SIGNAL(activated(int)),
    //         this, SLOT(networkError(int )));
    // error_notifier->setEnabled(true);

    timer = new QTimer(this);
    timer->setSingleShot(false);
    timer->setInterval(1000);
    connect(timer, SIGNAL(timeout()), this, SLOT(sshStateCheck()));
    timer->start();
}


void QSSHSession::reconnect() {
   disconnect();
   connect_to();
}

void QSSHSession::disconnect() {
    if (read_notifier)
      read_notifier->setEnabled(false);
    if (timer)
      timer->stop();
    if (channel)
      ssh_channel_free(channel);
    if (session) {
      ssh_disconnect(session);
      ssh_free(session);
    }
    //ssh_finalize();
    qDebug() << "SSH session is closing ...";
}

void QSSHSession::reset() {
    reconnect();
}


void QSSHSession::connect_to() {
	qDebug() << "ssh is initializing";
    ssh_threads_set_callbacks(ssh_threads_get_pthread());
    ssh_init();
    char * msg = "Connecting to host ...\r\n";
    emit dispatchData(msg, strlen(msg));
    session = ssh_new();
    int verbosity = 1;
    if (!qterm->siteInfo.username.isNull())
      ssh_options_set(session, SSH_OPTIONS_USER, qterm->siteInfo.username.toLatin1().data());

    ssh_options_set(session, SSH_OPTIONS_HOST, qterm->siteInfo.hostname.toLatin1().data());
    ssh_options_set(session, SSH_OPTIONS_PORT, &qterm->siteInfo.port);
    ssh_options_set(session, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
    int rc = ssh_connect(session);

    if (rc != SSH_OK) {
        qDebug() << "Connection to host failed!";
        char * msg = "\r\nFail to connect to server, press enter to restart ...";
        emit dispatchData(msg, strlen(msg));
        emit sessionError(1);
        return;
    }

    int known = ssh_is_server_known(session);
    int state= 0; //verify_knownhost(session);
    if (state != 0)
       return ;

    char * banner=ssh_get_issue_banner(session);
    if(banner) {
         qDebug()<<banner;
         free(banner);
    }

    authenticate_console(session);
    shell(session);
    }

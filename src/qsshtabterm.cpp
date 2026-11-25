#include "qsshtabterm.h"

void qsshTabTerm::init() {
	
	setWindowTitle(QString::fromUtf8("QSSHTerm"));
	initMenu();
	initToolbar();
  

	QWidget *centralWidget = new QWidget(this);
  tabs = new QTabWidget(centralWidget);
  QTabBar * tabBar =  tabs->tabBar();
  tabBar->setMovable(true);
  tabs->setTabsClosable(true);
  connect(tabs, SIGNAL(tabCloseRequested(int)), this, SLOT(closeTab(int)));
  connect(tabs, SIGNAL(currentChanged(int)), this, SLOT(tabSelected(int)));
  QGridLayout *gridLayout = new QGridLayout(centralWidget);

  gridLayout->addWidget(tabs,0,0,1,1);
  this->setCentralWidget(centralWidget);
  tabBar->installEventFilter(this);
  showSessionMgrDialog();
 
}

QSSHTerm *qsshTabTerm::termAt(int index) const {
    return qobject_cast<QSSHTerm*>(tabs->widget(index));
}

// Tabs are looked up by key instead of a cached key->index map, which went
// stale whenever tabs were moved or closed.
int qsshTabTerm::indexOfTerm(const QString &termKey) const {
    for (int i = 0; i < tabs->count(); ++i) {
        QSSHTerm *term = termAt(i);
        if (term && term->getTermKey() == termKey)
            return i;
    }
    return -1;
}

void qsshTabTerm::changeTabIcon(QString termKey, SSHTermState state){
    int idx = tabs->currentIndex() ;

    int index = indexOfTerm(termKey);
    if (index < 0)
        return;   // tab already closed
    QIcon tabIcon = tabs->tabIcon(index);

    if (state == BUSY) {
        if (idx == index)
          return;

       if (tabIcon.name() == "./icon/red.ico")
          return;
       else  {
         QIcon icon("./icon/red.ico"); 
         tabs->setTabIcon(index, icon);
       }

    } else if (state == IDLE){
      if (tabIcon.name() == "./icon/green.ico")
          return;
       else  {
         QIcon icon("./icon/green.ico"); 
         tabs->setTabIcon(index, icon);
       }
    } else if (state == DISCONNECT) {
      if (tabIcon.name() == "./icon/grey.ico")
          return;
       else  {
         QIcon icon("./icon/grey.ico"); 
         tabs->setTabIcon(index, icon);
       }
    }
}

void qsshTabTerm::tabSelected(int idx) {
        if (idx == -1)
            return;
        QSSHTerm * term = termAt(idx);
        if (term && term->state() == BUSY) {
          this->changeTabIcon(term->getTermKey(), IDLE);
          term->setState(IDLE);
        }
}

bool qsshTabTerm::eventFilter(QObject *obj, QEvent *event){
        if (obj != tabs->tabBar())
                return QObject::eventFilter(obj, event);

        if (event->type() != QEvent::MouseButtonPress)
                return QObject::eventFilter(obj, event);

        // compute the tab number (the filter sits on the tab bar, so the
        // position is already in tab bar coordinates)
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        QPoint position = mouseEvent->position().toPoint();
        int clickedItem = tabs->tabBar()->tabAt(position);

        // just in case
        if (clickedItem == -1)
                return QObject::eventFilter(obj, event);
 
        switch( mouseEvent->button() )
        {
                case Qt::LeftButton:
                        return QObject::eventFilter(obj, event);
                        break;
 
                case Qt::RightButton:
                        on_tab_rightMouse_pressed( clickedItem, position );
                        break;
 
                case Qt::MiddleButton:
                        return QObject::eventFilter(obj, event);
                        break;
 
                default:
                        return QObject::eventFilter(obj, event);
        }
 
        return true;
}

void qsshTabTerm::closeTab(int index) {
    QSSHTerm * term = termAt(index);
    if (!term)
        return;
    disconnect(term, nullptr, this, nullptr);
    tabs->removeTab(index);
    // removeTab() does not delete the page; the destructor closes the SSH session
    term->deleteLater();

    if (tabs->count() == 0) {
      sftp_mgr->setDisabled(true);
    }
}

void qsshTabTerm::on_tab_rightMouse_pressed( int clickedItem, QPoint pos ){
   qDebug() << "tab is clicked!";
   QMenu contextMenu(tr("Context menu"), tabs);

   QAction closeAct("Close", tabs);
   QAction resetAct("Reset", tabs);
   QAction connectAct("Connect", tabs);
   QAction duplicateAct("Duplicate", tabs);
   //connect(&action1, SIGNAL(triggered()), this, SLOT(removeDataPoint()));
   contextMenu.addAction(&duplicateAct);
   contextMenu.addAction(&connectAct);
   contextMenu.addAction(&resetAct);
   contextMenu.addAction(&closeAct);
   connect(&duplicateAct, &QAction::triggered, this, [this, clickedItem]() {
        this->openSession(clickedItem);
   });
   connect(&connectAct, &QAction::triggered, this, [this, clickedItem]() {
        if (QSSHTerm *term = termAt(clickedItem))
            emit term->reconnect();
   });
   connect(&resetAct, &QAction::triggered, this, [this, clickedItem]() {
        if (QSSHTerm *term = termAt(clickedItem))
            emit term->reset();
   });
   connect(&closeAct, &QAction::triggered, this, [this, clickedItem]() {
        this->closeTab(clickedItem);
   });
   contextMenu.exec(tabs->tabBar()->mapToGlobal(pos));
}

void qsshTabTerm::initMenu() {
	  QMenu *session = this->menuBar()->addMenu(QObject::tr("Session"));
    QMenu *view = this->menuBar()->addMenu(QObject::tr("View"));
    QAction * mtoolbar = view->addAction(QObject::tr("&Toolbar"));
    QAction * menubar = view->addAction(QObject::tr("&Menubar"));
    QAction * mfind = view->addAction(QObject::tr("&Find"));
    QShortcut * findshortcut = new QShortcut(QKeySequence("Ctrl+f"), this);
    
    QMenu *help = this->menuBar()->addMenu(QObject::tr("Help"));
    QAction * about = help->addAction(QObject::tr("&About"));
    QAction * sessionMgr = session->addAction(QObject::tr("Session &Manager"));
    QAction *exit = session->addAction(QObject::tr("&Exit"));

    connect(sessionMgr, &QAction::triggered, this, &qsshTabTerm::showSessionMgrDialog);
    connect(mfind, &QAction::triggered, this, &qsshTabTerm::showFindDialog);
    connect(about, &QAction::triggered, this, &qsshTabTerm::showAboutDialog);
    connect(exit, &QAction::triggered, []() {
      QCoreApplication::exit() ;
    });
    connect(findshortcut, &QShortcut::activated, this, &qsshTabTerm::showFindDialog);
}

void qsshTabTerm::showFindDialog() {
   int idx = tabs->currentIndex() ;
   if (idx == -1)
      return;
   if (QSSHTerm * term = termAt(idx))
       term->toggleShowSearchBar();
}

void qsshTabTerm::initToolbar() {
	QIcon icon("./icon/structure.ico"); 
  QIcon sftp_icon("./icon/sftp.ico"); 
	QToolBar *toolbar = this->addToolBar("main toolbar");
  QAction * mgr = toolbar->addAction(icon, "sessionMgr");
  sftp_mgr = toolbar->addAction(sftp_icon, "sftpMgr");
  QShortcut * sessionshortcut = new QShortcut(QKeySequence("Alt+c"), this);
  
  sftp_mgr->setDisabled(true);   // enabled once a session tab exists
  connect(mgr, &QAction::triggered, this, &qsshTabTerm::showSessionMgrDialog);
  connect(sftp_mgr, &QAction::triggered, this, &qsshTabTerm::showSftpMgrDialog);
  connect(sessionshortcut, &QShortcut::activated, this, &qsshTabTerm::showSessionMgrDialog);
}

void qsshTabTerm::showSessionMgrDialog() {
    if (!sessionMgr_dialog) {
    	  sessionMgr_dialog=new QSiteTreeDialog(this);
        connect(sessionMgr_dialog, SIGNAL(notifyNewSession(SiteInfo)), this, SLOT(openSession(SiteInfo)));
        sessionMgr_dialog->setModal(true);
    } 

    sessionMgr_dialog->show();
}

void qsshTabTerm::showSftpMgrDialog() {
    QSSHTerm * term = termAt(tabs->currentIndex());
    if (!term)
        return;
   if (!sftp_dialog) {
        sftp_dialog=new QSSHTerm_Sftp_Dialog(this);
        sftp_dialog->setModal(true);
    } 
    
    sftp_dialog->setSiteInfo(term->siteInfo);
    sftp_dialog->show();
}

void qsshTabTerm::showAboutDialog() {
    QMessageBox::information(this, "About", "QSSHTerm V0.1a \nAuthor: David.Fan \nLicense: LGPL", QMessageBox::Ok, QMessageBox::Ok);
}

void qsshTabTerm::openSession(SiteInfo info) {

    QString labelTitle = info.sitename;
    int maxIndex = 0;
    for (int j = 0; j< tabs->count(); j++) {
        QString title = tabs->tabText(j);
        if (title.startsWith(labelTitle)) {
            if (title == labelTitle && maxIndex == 0)  {
                maxIndex = 1;
                continue;
            } else {
                QString possibleNumberPair = title.mid(labelTitle.length(), title.length() - labelTitle.length());
                if (possibleNumberPair.startsWith("(") && possibleNumberPair.endsWith(")")) {
                    QString number = possibleNumberPair.mid(1, possibleNumberPair.length() - 2);
                    bool ok;
                    int tabNumber = number.toInt(&ok, 10); 
                    if (ok && maxIndex <= tabNumber) {
                        maxIndex = tabNumber + 1;
                    }
                }
            }
        }

    }

    if (maxIndex > 0) {
        labelTitle = labelTitle + "(" + QString::number(maxIndex) + ")";
    } 

    QSSHTerm *term = new QSSHTerm(info);
    term->setTabTermKey(labelTitle);
    // connect before start() so no state change is missed
    connect(term, &QSSHTerm::icon_change, this, &qsshTabTerm::changeTabIcon);
    int idx = tabs->addTab(term, labelTitle);
    QIcon icon("./icon/green.ico");
    tabs->setTabIcon(idx, icon);
    tabs->setCurrentIndex(idx);
    term->start();
    term->setFocus();
    if (sessionMgr_dialog)
        sessionMgr_dialog->hide();
    sftp_mgr->setEnabled(true);
}

void qsshTabTerm::openSession(int index) {
     if (QSSHTerm * term = termAt(index))
         this->openSession(term->siteInfo);
}
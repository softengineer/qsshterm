#include "qsshtabterm.h"

void qsshTabTerm::init() {
	
	setWindowTitle(QString::fromUtf8("QSSHTerm"));
	initMenu();
	initToolbar();
  

	QWidget *centralWidget = new QWidget(this);
  tabs = new QTabWidget(centralWidget);
  QTabBar * tabBar =  tabs->tabBar();
  tabBar->setMovable(true);
  tabs->setStyleSheet("QWidget#custom_tab, QWidget#templates_tab{\
                         background-color: red; \    
                      }");
  tabs->setTabsClosable(true);
  connect(tabs, SIGNAL(tabCloseRequested(int)), this, SLOT(closeTab(int)));
  connect(tabs, SIGNAL(currentChanged(int)), this, SLOT(tabSelected(int)));
  connect(tabBar, SIGNAL(tabMoved(int, int)), this, SLOT(tabMoved(int, int)));
  QGridLayout *gridLayout = new QGridLayout(centralWidget);

  gridLayout->addWidget(tabs,0,0,1,1);
  this->setCentralWidget(centralWidget);
  tabs->installEventFilter(this);
  this->termKeyHash.clear();
  showSessionMgrDialog();
 
}

void qsshTabTerm::tabMoved(int to, int from) {
  qDebug() << "received move signal, from: " << from << ", to :" << to ;
   if (from == to)
      return;
   QHashIterator<QString, int> itr(termKeyHash);
   QString termKey;

   while (itr.hasNext()) {
        itr.next();
        if (itr.value() == from) 
            termKey = itr.key();
   } 

   itr = QHashIterator<QString, int> (termKeyHash);
   while (itr.hasNext()) {
        itr.next();
        if (itr.key().length() ==0)
          continue;

        if (from > to)  { 
           // move backward
          if (itr.value() >= to && itr.value() < from) {
            termKeyHash[itr.key()] = itr.value() + 1;
          }
        }  else {
          // move forward
          if (itr.value() > from && itr.value() <= to) {
            termKeyHash[itr.key()] = itr.value() - 1;
          }
        }
    } 

    this->termKeyHash[termKey] = to;
}

void qsshTabTerm::changeTabIcon(QString termKey, bool isBusy){
    int idx = tabs->currentIndex() ;

    int index = termKeyHash[termKey];
    QIcon tabIcon = tabs->tabIcon(index);

    if (isBusy) {
        if (idx == index)
          return;

       if (tabIcon.name() == "./icon/red.ico")
          return;
       else  {
         QIcon icon("./icon/red.ico"); 
         tabs->setTabIcon(index, icon);
       }

    } else {
      if (tabIcon.name() == "./icon/green.ico")
          return;
       else  {
         QIcon icon("./icon/green.ico"); 
         tabs->setTabIcon(index, icon);
       }
    }
}

void qsshTabTerm::tabSelected(int idx) {
        if (idx == -1)
            return;
        QSSHTerm * term = static_cast<QSSHTerm*> (tabs->widget(idx));
        this->changeTabIcon(term->getTermKey(), false);
}

bool qsshTabTerm::eventFilter(QObject *obj, QEvent *event){
        if (obj != tabs)
                return QObject::eventFilter(obj, event);

        
 
        if (event->type() != QEvent::MouseButtonPress)
                return QObject::eventFilter(obj, event);
 
        // compute the tab number
        QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
        QPoint position = mouseEvent->pos();
        int c = tabs->tabBar()->count();
        int clickedItem = -1;
 
        for (int i=0; i<c; i++)
        {
                if ( tabs->tabBar()->tabRect(i).contains( position ) )
                {
                        clickedItem = i;
                        break;
                }
        }
 
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
 
                case Qt::MidButton:
                        return QObject::eventFilter(obj, event);
                        break;
 
                default:
                        return QObject::eventFilter(obj, event);
        }
 
        return true;
}

void qsshTabTerm::closeTab(int index) {
    QSSHTerm * term = static_cast<QSSHTerm*> (tabs->widget(index));
    emit term->disconnect();
    disconnect(term, 0, 0, 0);
    tabs->removeTab(index);
    this->termKeyHash.remove(term->getTermKey());
    QHashIterator<QString, int> itr(termKeyHash);
    while (itr.hasNext()) {
        itr.next();
        if (itr.value() > index) {
          termKeyHash[itr.key()] = itr.value() - 1;
        }
    } 

    if (tabs->tabBar()->count() ==0 ) {
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
   connect(&duplicateAct, &QAction::triggered, [this, clickedItem]() {
        this->openSession(clickedItem);
   });
   contextMenu.exec(tabs->mapToGlobal(pos));
   disconnect(&duplicateAct, 0, 0, 0);
  
}

void qsshTabTerm::initMenu() {
	QMenu *session = this->menuBar()->addMenu(QObject::tr("Session"));
    QMenu *view = this->menuBar()->addMenu(QObject::tr("View"));
    QAction * mtoolbar = view->addAction(QObject::tr("&Toolbar"));
    QAction * menubar = view->addAction(QObject::tr("&Menubar"));
    QMenu *help = this->menuBar()->addMenu(QObject::tr("Help"));
    QAction * about = help->addAction(QObject::tr("&About"));
    QAction * sessionMgr = session->addAction(QObject::tr("Session &Manager"));
    QAction *exit = session->addAction(QObject::tr("&Exit"));

    connect(sessionMgr, &QAction::triggered, this, &qsshTabTerm::showSessionMgrDialog);
    connect(about, &QAction::triggered, this, &qsshTabTerm::showAboutDialog);
    connect(exit, &QAction::triggered, []() {
      QCoreApplication::exit() ;
    });
    
}

void qsshTabTerm::initToolbar() {
	QIcon icon("./icon/structure.ico"); 
  QIcon sftp_icon("./icon/sftp.ico"); 
	QToolBar *toolbar = this->addToolBar("main toolbar");
  QAction * mgr = toolbar->addAction(icon, "sessionMgr");
  sftp_mgr = toolbar->addAction(sftp_icon, "sftpMgr");
  QShortcut * sessionshortcut = new QShortcut(QKeySequence("Alt+c"), this);
  
  //sftp_mgr->setDisabled(true);
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
    int index = tabs->currentIndex();
    QSSHTerm * term = static_cast<QSSHTerm*> (tabs->widget(index));
   if (!sftp_dialog) {
    qDebug() <<" init sftp dialog";
        sftp_dialog=new QSSHTerm_Sftp_Dialog(this);
        sftp_dialog->setModal(true);
    } 
    
    sftp_dialog->setSiteInfo(term->siteInfo);
    sftp_dialog->show();
}

void qsshTabTerm::showAboutDialog() {
    QMessageBox::information(NULL, "About", "QSSHTerm V0.1a \nAuthor: David.Fan \nLicense: LGPL", QMessageBox::Ok, QMessageBox::Ok);
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
    int idx = tabs->addTab(term, labelTitle);

    this->termKeyHash[labelTitle] = idx;
    term->setTabTermKey(labelTitle);
    term->start();
    tabs->setCurrentIndex(idx);
    term->setFocus();
    sessionMgr_dialog->hide();
    QIcon icon("./icon/green.ico"); 
    tabs->setTabIcon(idx, icon);
    sftp_mgr->setEnabled(true);
    connect(term, SIGNAL(icon_change(QString, bool)), this, SLOT(changeTabIcon(QString, bool)));
  
}

void qsshTabTerm::openSession(int index) {
     QSSHTerm * term = static_cast<QSSHTerm*> (tabs->widget(index));
     this->openSession(term->siteInfo);
}
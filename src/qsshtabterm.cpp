#include "qsshtabterm.h"

void qsshTabTerm::init() {
	
	setWindowTitle(QString::fromUtf8("QSSHTerm"));
	initMenu();
	initToolbar();

	QWidget *centralWidget = new QWidget(this);
    tabs = new QTabWidget(centralWidget);
    tabs->setStyleSheet("QWidget#custom_tab, QWidget#templates_tab{\
                           background-color: red; \    
                        }");
    tabs->setTabsClosable(true);
    connect(tabs, SIGNAL(tabCloseRequested(int)), this, SLOT(closeTab(int)));
    connect(tabs, SIGNAL(currentChanged(int)), this, SLOT(tabSelected(int)));
    QGridLayout *gridLayout = new QGridLayout(centralWidget);

    gridLayout->addWidget(tabs,0,0,1,1);
    this->setCentralWidget(centralWidget);
    tabs->installEventFilter(this);
    showSessionMgrDialog();
 
}

void qsshTabTerm::changeTabIcon(int index, bool isBusy){
    int idx = tabs->currentIndex() ;

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

        this->changeTabIcon(idx, false);
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

   contextMenu.exec(tabs->mapToGlobal(pos));
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
}

void qsshTabTerm::initToolbar() {
	QIcon icon("./icon/structure.ico"); 
	QToolBar *toolbar = this->addToolBar("main toolbar");
    QAction * mgr = toolbar->addAction(icon, "sessionMgr");
    connect(mgr, &QAction::triggered, this, &qsshTabTerm::showSessionMgrDialog);
}

void qsshTabTerm::showSessionMgrDialog() {
    if (!sessionMgr_dialog) {
    	sessionMgr_dialog=new QSiteTreeDialog(this);
        connect(sessionMgr_dialog, SIGNAL(notifyNewSession(SiteInfo)), this, SLOT(openSession(SiteInfo)));
        sessionMgr_dialog->setModal(true);
    } 

    sessionMgr_dialog->show();
}

void qsshTabTerm::showAboutDialog() {
    QMessageBox::information(NULL, "About", "QSSHTerm V0.1 \nAuthor: david.fan \nLicense: LGPL V3.0 ", QMessageBox::Ok, QMessageBox::Ok);
}

void qsshTabTerm::openSession(SiteInfo info) {

    QString labelTitle = info.sitename;
    int maxIndex = 0;
    for (int j = 0; j< tabs->count(); j++) {
        QString title = tabs->tabText(j);
        if (title.startsWith(labelTitle)) {
            if (title == labelTitle)  {
                maxIndex = 1;
                continue;
            } else {
                QString possibleNumberPair = title.mid(labelTitle.length(), title.length() - labelTitle.length());
                if (possibleNumberPair.startsWith("(") && possibleNumberPair.endsWith(")")) {
                    QString number = possibleNumberPair.mid(1, possibleNumberPair.length() - 2);
                    bool ok;
                    int tabNumber = number.toInt(&ok, 10); 
                    if (ok) {
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
    term->setTabIndex(idx);
    term->start();
    tabs->setCurrentIndex(idx);
    term->setFocus();
    sessionMgr_dialog->hide();
    QIcon icon("./icon/green.ico"); 
    tabs->setTabIcon(idx, icon);
    connect(term, SIGNAL(icon_change(int, bool)), this, SLOT(changeTabIcon(int, bool)));
}
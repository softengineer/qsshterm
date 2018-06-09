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

    QGridLayout *gridLayout = new QGridLayout(centralWidget);

    gridLayout->addWidget(tabs,0,0,1,1);
    this->setCentralWidget(centralWidget);
    tabs->installEventFilter(this);
    showSessionMgrDialog();
 
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
    QSSHTerm *term = new QSSHTerm(info);
    int idx = tabs->addTab(term, info.sitename);
    term->setTabIndex(idx);
    term->start();
    tabs->setCurrentIndex(idx);
    term->setFocus();
    sessionMgr_dialog->hide();
    QIcon icon("./icon/green.ico"); 
    tabs->setTabIcon(idx, icon);
}
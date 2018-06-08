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
    //tabs->setFixedSize(height, width);
    this->setCentralWidget(centralWidget);

    showSessionMgrDialog();
    //  QSSHTerm *term = new QSSHTerm();
    // int idx = tabs->addTab(term,"sshSession1");
    // term->setTabIndex(idx);
    // term->start();
    //tabs->setTabIcon(idx, icon);
}

void qsshTabTerm::closeTab(int index) {
    qDebug() << "close event trigger " << index;
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
    QMessageBox::information(NULL, "About", "QSSHTerm V0.1 \n Author: david.fan", QMessageBox::Ok, QMessageBox::Ok);
}

void qsshTabTerm::openSession(SiteInfo info) {
    QSSHTerm *term = new QSSHTerm(info);
    int idx = tabs->addTab(term, info.sitename);
    term->setTabIndex(idx);
    term->start();
    tabs->setCurrentIndex(idx);
    sessionMgr_dialog->hide();
    QIcon icon("./icon/green.ico"); 
    tabs->setTabIcon(idx, icon);
}
#include "qsshtabterm.h"
#include "qssh_icons.h"
#include "qssh_theme.h"

using qssh::StatusIcon;

static QString settingsPath()
{
    return QDir::homePath() + "/.qsshterm/qsshterm.ini";
}

static QString siteAddress(const SiteInfo &s)
{
    return QStringLiteral("%1%2:%3")
        .arg(s.username.isEmpty() ? QString() : s.username + '@', s.hostname)
        .arg(s.port > 0 ? s.port : 22);
}

static QLabel *statusField(QWidget *parent)
{
    QLabel *label = new QLabel(parent);
    label->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    label->setContentsMargins(6, 0, 6, 0);
    return label;
}

qsshTabTerm::qsshTabTerm(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("QSSHTerm"));
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks);

    tabs = new QTabWidget(this);
    tabs->setDocumentMode(true);
    tabs->setTabsClosable(true);
    tabs->setMovable(true);
    tabs->setUsesScrollButtons(true);
    tabs->setElideMode(Qt::ElideRight);
    tabs->tabBar()->setIconSize(QSize(14, 14));
    tabs->tabBar()->installEventFilter(this);
    connect(tabs, &QTabWidget::tabCloseRequested, this, &qsshTabTerm::closeTab);
    connect(tabs, &QTabWidget::currentChanged, this, &qsshTabTerm::tabSelected);
    setCentralWidget(tabs);

    initDock();
    initActions();
    initStatusBar();

    QSettings settings(settingsPath(), QSettings::IniFormat);
    if (!restoreGeometry(settings.value("mainWindow/geometry").toByteArray()))
        resize(1200, 760);
    restoreState(settings.value("mainWindow/state").toByteArray());

    updateSessionActions();
    updateStatus();
    QTimer::singleShot(0, this, &qsshTabTerm::showStartupDialog);
}

// ---------------------------------------------------------------------------
// layout

void qsshTabTerm::initDock()
{
    manager = new QSessionManagerWidget;
    connect(manager, &QSessionManagerWidget::connectRequested, this,
            qOverload<SiteInfo>(&qsshTabTerm::openSession));

    managerDock = new QDockWidget(tr("Session Manager"), this);
    managerDock->setObjectName("sessionManagerDock");
    managerDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    managerDock->setWidget(manager);
    managerDock->setMinimumWidth(200);
    addDockWidget(Qt::LeftDockWidgetArea, managerDock);
    resizeDocks({managerDock}, {250}, Qt::Horizontal);
}

void qsshTabTerm::initActions()
{
    auto make = [this](const QIcon &icon, const QString &text, const QKeySequence &key,
                       const QString &tip, auto slot) {
        QAction *a = new QAction(icon, text, this);
        if (!key.isEmpty())
            a->setShortcut(key);
        QString t = tip.isEmpty() ? QString(text).remove('&').remove("...") : tip;
        if (!key.isEmpty())
            t += QStringLiteral(" (%1)").arg(key.toString(QKeySequence::NativeText));
        a->setToolTip(t);
        a->setStatusTip(t);
        connect(a, &QAction::triggered, this, slot);
        addAction(a);   // shortcuts keep working with the menu bar hidden
        return a;
    };
    auto onTerm = [this](auto fn) {
        return [this, fn]() {
            if (QSSHTerm *term = currentTerm())
                fn(term);
        };
    };

    // --- File
    QAction *actConnect = make(qssh::themeIcon({"network-server", "computer"}, QStyle::SP_ComputerIcon),
                               tr("&Connect..."), QKeySequence(Qt::ALT | Qt::Key_C),
                               tr("Open the Connect dialog"), [this]() { showConnectDialog(); });
    QAction *actQuick = make(qssh::boltIcon(),
                             tr("&Quick Connect..."), QKeySequence(Qt::ALT | Qt::Key_Q),
                             tr("Connect to a host without saving a session"),
                             [this]() { showQuickConnectDialog(); });
    QAction *actReconnect = make(qssh::themeIcon({"view-refresh"}, QStyle::SP_BrowserReload),
                                 tr("&Reconnect"), QKeySequence(),
                                 tr("Reconnect the current session"),
                                 onTerm([](QSSHTerm *t) { emit t->reconnect(); }));
    QAction *actDisconnect = make(qssh::themeIcon({"process-stop"}, QStyle::SP_BrowserStop),
                                  tr("&Disconnect"), QKeySequence(),
                                  tr("Disconnect the current session"),
                                  onTerm([](QSSHTerm *t) {
                                      if (!t->isConnected())
                                          return;
                                      emit t->disconnectSession();
                                      t->sessionError(0);
                                      t->writeData("\r\nDisconnected. Press Enter to reconnect ...\r\n");
                                  }));
    QAction *actDuplicate = make(qssh::themeIcon({"tab-new", "list-add"}, QStyle::SP_FileDialogNewFolder),
                                 tr("D&uplicate Session"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T),
                                 tr("Open the current session again in a new tab"),
                                 [this]() { openSession(tabs->currentIndex()); });
    QAction *actCloseTab = make(qssh::themeIcon({"window-close"}, QStyle::SP_DialogCloseButton),
                                tr("C&lose Tab"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W),
                                QString(), [this]() { closeTab(tabs->currentIndex()); });
    QAction *actExit = make(qssh::themeIcon({"application-exit"}, QStyle::SP_DialogCloseButton),
                            tr("E&xit"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Q),
                            QString(), [this]() { close(); });

    // --- Edit
    QAction *actCopy = make(qssh::themeIcon({"edit-copy"}, QStyle::SP_FileIcon),
                            tr("&Copy"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C), QString(),
                            onTerm([](QSSHTerm *t) { t->copyClipboard(); }));
    QAction *actPaste = make(qssh::themeIcon({"edit-paste"}, QStyle::SP_DialogApplyButton),
                             tr("&Paste"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V), QString(),
                             onTerm([](QSSHTerm *t) { t->pasteClipboard(); }));
    QAction *actFind = make(qssh::themeIcon({"edit-find"}, QStyle::SP_FileDialogContentsView),
                            tr("&Find..."), QKeySequence(Qt::CTRL | Qt::Key_F),
                            tr("Search in the current terminal"), [this]() { showFindDialog(); });
    QAction *actClear = make(qssh::themeIcon({"edit-clear"}, QStyle::SP_LineEditClearButton),
                             tr("C&lear Scrollback"), QKeySequence(), QString(),
                             onTerm([](QSSHTerm *t) { t->clear(); }));
    QAction *actZoomIn = make(qssh::themeIcon({"zoom-in"}, QStyle::SP_ArrowUp),
                              tr("Zoom &In"), QKeySequence(Qt::CTRL | Qt::Key_Equal), QString(),
                              onTerm([](QSSHTerm *t) { t->zoomIn(); }));
    QAction *actZoomOut = make(qssh::themeIcon({"zoom-out"}, QStyle::SP_ArrowDown),
                               tr("Zoom &Out"), QKeySequence(Qt::CTRL | Qt::Key_Minus), QString(),
                               onTerm([](QSSHTerm *t) { t->zoomOut(); }));
    QAction *actZoomReset = make(qssh::themeIcon({"zoom-original"}, QStyle::SP_DialogResetButton),
                                 tr("&Reset Zoom"), QKeySequence(Qt::CTRL | Qt::Key_0), QString(),
                                 onTerm([](QSSHTerm *t) { t->changeFont(); }));

    // --- Transfer
    actSftp = make(qssh::themeIcon({"folder-remote"}, QStyle::SP_DriveNetIcon),
                   tr("&SFTP File Manager..."), QKeySequence(Qt::ALT | Qt::Key_P),
                   tr("Browse and transfer files of the current session"),
                   [this]() { showSftpMgrDialog(); });

    // --- Window
    QAction *actNextTab = make(QIcon(), tr("&Next Tab"), QKeySequence(Qt::CTRL | Qt::Key_PageDown),
                               QString(), [this]() {
                                   if (tabs->count() > 1)
                                       tabs->setCurrentIndex((tabs->currentIndex() + 1) % tabs->count());
                               });
    QAction *actPrevTab = make(QIcon(), tr("&Previous Tab"), QKeySequence(Qt::CTRL | Qt::Key_PageUp),
                               QString(), [this]() {
                                   if (tabs->count() > 1)
                                       tabs->setCurrentIndex((tabs->currentIndex() + tabs->count() - 1) % tabs->count());
                               });

    // --- View toggles
    QToolBar *toolbar = new QToolBar(tr("Main Toolbar"), this);
    toolbar->setObjectName("mainToolbar");
    toolbar->setIconSize(QSize(18, 18));
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setMovable(false);

    QAction *actMenuBar = make(QIcon(), tr("&Menu Bar"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M),
                               QString(), [this](bool on) { menuBar()->setVisible(on); });
    actMenuBar->setCheckable(true);
    actMenuBar->setChecked(true);
    QAction *actToolbar = toolbar->toggleViewAction();
    actToolbar->setText(tr("&Toolbar"));
    QAction *actManager = managerDock->toggleViewAction();
    actManager->setText(tr("&Session Manager"));
    actManager->setIcon(qssh::themeIcon({"view-list-tree", "view-list", "folder"}, QStyle::SP_DirIcon));
    actManager->setToolTip(tr("Show or hide the Session Manager"));
    QAction *actStatusBar = make(QIcon(), tr("St&atus Bar"), QKeySequence(), QString(),
                                 [this](bool on) { statusBar()->setVisible(on); });
    actStatusBar->setCheckable(true);
    actStatusBar->setChecked(true);
    QAction *actFullScreen = make(qssh::themeIcon({"view-fullscreen"}, QStyle::SP_TitleBarMaxButton),
                                  tr("&Full Screen"), QKeySequence(Qt::Key_F11), QString(),
                                  [this]() { setWindowState(windowState() ^ Qt::WindowFullScreen); });

    QAction *actAbout = make(qssh::themeIcon({"help-about"}, QStyle::SP_MessageBoxInformation),
                             tr("&About QSSHTerm"), QKeySequence(), QString(),
                             [this]() { showAboutDialog(); });

    // --- menus
    QMenu *file = menuBar()->addMenu(tr("&File"));
    file->addActions({actConnect, actQuick});
    file->addSeparator();
    file->addActions({actReconnect, actDisconnect, actDuplicate});
    file->addSeparator();
    file->addAction(actCloseTab);
    file->addSeparator();
    file->addAction(actExit);

    QMenu *edit = menuBar()->addMenu(tr("&Edit"));
    edit->addActions({actCopy, actPaste});
    edit->addSeparator();
    edit->addActions({actFind, actClear});

    QMenu *view = menuBar()->addMenu(tr("&View"));
    view->addActions({actMenuBar, actToolbar, actManager, actStatusBar});
    view->addSeparator();

    ThemeManager *themes = ThemeManager::instance();
    QMenu *themeMenu = view->addMenu(qssh::themeIcon({"preferences-desktop-theme", "applications-graphics"},
                                                     QStyle::SP_DesktopIcon), tr("T&heme"));
    QActionGroup *themeGroup = new QActionGroup(themeMenu);
    const QList<QPair<QString, ThemeManager::Theme>> themeChoices = {
        {tr("&System"), ThemeManager::System}, {tr("&Light"), ThemeManager::Light},
        {tr("&Dark"), ThemeManager::Dark}};
    for (const auto &choice : themeChoices) {
        QAction *a = themeMenu->addAction(choice.first);
        a->setCheckable(true);
        a->setChecked(themes->theme() == choice.second);
        themeGroup->addAction(a);
        const ThemeManager::Theme t = choice.second;
        connect(a, &QAction::triggered, themes, [themes, t]() { themes->setTheme(t); });
    }

    // filled on open, so it always shows the current choice
    QMenu *schemeMenu = view->addMenu(tr("Terminal &Colors"));
    connect(schemeMenu, &QMenu::aboutToShow, this, [schemeMenu, themes]() {
        schemeMenu->clear();
        QActionGroup *group = new QActionGroup(schemeMenu);
        auto add = [&](const QString &label, const QString &value) {
            QAction *a = schemeMenu->addAction(label);
            a->setCheckable(true);
            a->setChecked(themes->terminalSchemeSetting() == value);
            group->addAction(a);
            connect(a, &QAction::triggered, themes, [themes, value]() { themes->setTerminalScheme(value); });
        };
        add(tr("Auto (follow theme)"), QStringLiteral("Auto"));
        schemeMenu->addSeparator();
        for (const QString &name : ThemeManager::availableTerminalSchemes())
            add(name, name);
    });
    view->addSeparator();
    view->addActions({actZoomIn, actZoomOut, actZoomReset});
    view->addSeparator();
    view->addAction(actFullScreen);

    QMenu *transfer = menuBar()->addMenu(tr("&Transfer"));
    transfer->addAction(actSftp);

    QMenu *window = menuBar()->addMenu(tr("&Window"));
    connect(window, &QMenu::aboutToShow, this, [this, window, actNextTab, actPrevTab]() {
        window->clear();
        window->addActions({actNextTab, actPrevTab});
        if (tabs->count() == 0)
            return;
        window->addSeparator();
        QActionGroup *group = new QActionGroup(window);
        for (int i = 0; i < tabs->count(); ++i) {
            QAction *a = window->addAction(tabs->tabIcon(i), tabs->tabText(i));
            a->setCheckable(true);
            a->setChecked(i == tabs->currentIndex());
            group->addAction(a);
            connect(a, &QAction::triggered, this, [this, i]() { tabs->setCurrentIndex(i); });
        }
    });

    QMenu *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(actAbout);
    help->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);

    // --- toolbar, grouped like SecureCRT
    quickConnect = new QLineEdit(toolbar);
    quickConnect->setPlaceholderText(tr("Enter host <Alt+R>"));
    quickConnect->setToolTip(tr("Quick connect: [user@]host[:port], or a saved session name"));
    quickConnect->setClearButtonEnabled(true);
    quickConnect->setMaximumWidth(220);
    connect(quickConnect, &QLineEdit::returnPressed, this, &qsshTabTerm::quickConnectFromBox);
    QSettings settings(settingsPath(), QSettings::IniFormat);
    QCompleter *completer = new QCompleter(settings.value("quickConnect/history").toStringList(), quickConnect);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    quickConnect->setCompleter(completer);
    QShortcut *focusQuick = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_R), this);
    connect(focusQuick, &QShortcut::activated, this, [this]() {
        quickConnect->setFocus();
        quickConnect->selectAll();
    });

    toolbar->addAction(actManager);
    toolbar->addSeparator();
    toolbar->addActions({actConnect, actQuick});
    toolbar->addWidget(quickConnect);
    toolbar->addSeparator();
    toolbar->addActions({actReconnect, actDisconnect, actDuplicate});
    toolbar->addSeparator();
    toolbar->addActions({actCopy, actPaste, actFind});
    toolbar->addSeparator();
    toolbar->addAction(actSftp);
    QWidget *spacer = new QWidget(toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);
    toolbar->addAction(actAbout);
    addToolBar(Qt::TopToolBarArea, toolbar);

    sessionActions = {actReconnect, actDisconnect, actDuplicate, actCloseTab, actCopy, actPaste,
                      actFind, actClear, actZoomIn, actZoomOut, actZoomReset, actSftp};
}

void qsshTabTerm::initStatusBar()
{
    statusMessage = new QLabel(tr("Ready"), this);
    statusMessage->setContentsMargins(4, 0, 4, 0);
    statusBar()->addWidget(statusMessage, 1);

    statusCipher = statusField(this);
    statusHost = statusField(this);
    statusSize = statusField(this);
    statusTerm = statusField(this);
    statusClock = statusField(this);
    for (QLabel *l : {statusCipher, statusHost, statusSize, statusTerm, statusClock})
        statusBar()->addPermanentWidget(l);

    auto tick = [this]() {
        statusClock->setText(QLocale().toString(QTime::currentTime(), QLocale::ShortFormat));
    };
    tick();
    QTimer *clock = new QTimer(this);
    connect(clock, &QTimer::timeout, this, tick);
    clock->start(15000);
}

void qsshTabTerm::updateStatus()
{
    QSSHTerm *term = currentTerm();
    const bool has = term != nullptr;
    for (QLabel *l : {statusCipher, statusHost, statusSize, statusTerm})
        l->setVisible(has);
    if (!has) {
        statusMessage->setText(tr("Ready"));
        setWindowTitle(QStringLiteral("QSSHTerm"));
        return;
    }
    const QString host = siteAddress(term->siteInfo);
    if (term->isConnected())
        statusMessage->setText(tr("Connected to %1").arg(term->siteInfo.hostname));
    else if (term->state() == DISCONNECT)
        statusMessage->setText(tr("Disconnected"));
    else
        statusMessage->setText(tr("Connecting to %1 ...").arg(term->siteInfo.hostname));
    statusCipher->setText(term->connectionInfo().isEmpty() ? tr("ssh2") : term->connectionInfo());
    statusHost->setText(host);
    statusSize->setText(tr("%1 Rows, %2 Cols").arg(term->screenLinesCount()).arg(term->screenColumnsCount()));
    statusTerm->setText(QStringLiteral("xterm"));
    setWindowTitle(QStringLiteral("%1 - QSSHTerm").arg(tabs->tabText(tabs->currentIndex())));
}

void qsshTabTerm::updateSessionActions()
{
    const bool has = tabs->count() > 0;
    for (QAction *a : sessionActions)
        a->setEnabled(has);
}

// ---------------------------------------------------------------------------
// tabs

QSSHTerm *qsshTabTerm::termAt(int index) const {
    return qobject_cast<QSSHTerm*>(tabs->widget(index));
}

QSSHTerm *qsshTabTerm::currentTerm() const {
    return termAt(tabs->currentIndex());
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
    // called for every chunk of remote output: must stay cheap. The shown
    // badge is tracked in the tab data so the icon is only set on changes.
    int index = indexOfTerm(termKey);
    if (index < 0)
        return;   // tab already closed
    QSSHTerm *term = termAt(index);
    if (state == BUSY && index == tabs->currentIndex())
        state = IDLE;   // the visible tab never shows activity

    StatusIcon kind = state == DISCONNECT ? StatusIcon::Disconnected
                    : !term->isConnected() ? StatusIcon::Connecting
                    : state == BUSY ? StatusIcon::Activity
                    : StatusIcon::Connected;

    QTabBar *bar = tabs->tabBar();
    QVariant shown = bar->tabData(index);
    if (shown.isValid() && shown.toInt() == int(kind))
        return;

    static const QIcon icons[] = {
        qssh::statusIcon(StatusIcon::Connected), qssh::statusIcon(StatusIcon::Activity),
        qssh::statusIcon(StatusIcon::Disconnected), qssh::statusIcon(StatusIcon::Connecting)};
    bar->setTabData(index, int(kind));
    tabs->setTabIcon(index, icons[int(kind)]);
}

void qsshTabTerm::tabSelected(int idx) {
    if (QSSHTerm *term = termAt(idx)) {
        if (term->state() == BUSY)
            term->setState(IDLE);
        changeTabIcon(term->getTermKey(), term->state());
        term->setFocus();
    }
    updateStatus();
}

bool qsshTabTerm::eventFilter(QObject *obj, QEvent *event){
    if (obj != tabs->tabBar() || event->type() != QEvent::MouseButtonPress)
        return QMainWindow::eventFilter(obj, event);

    // the filter sits on the tab bar, so the position is in tab bar coordinates
    QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
    QPoint position = mouseEvent->position().toPoint();
    int clickedItem = tabs->tabBar()->tabAt(position);
    if (clickedItem == -1)
        return QMainWindow::eventFilter(obj, event);

    switch (mouseEvent->button()) {
    case Qt::RightButton:
        on_tab_rightMouse_pressed(clickedItem, position);
        return true;
    case Qt::MiddleButton:
        closeTab(clickedItem);
        return true;
    default:
        return QMainWindow::eventFilter(obj, event);
    }
}

void qsshTabTerm::closeTab(int index) {
    QSSHTerm * term = termAt(index);
    if (!term)
        return;
    disconnect(term, nullptr, this, nullptr);
    tabs->removeTab(index);
    // removeTab() does not delete the page; the destructor closes the SSH session
    term->deleteLater();
    updateSessionActions();
    updateStatus();
}

void qsshTabTerm::on_tab_rightMouse_pressed(int clickedItem, QPoint pos) {
    QSSHTerm *term = termAt(clickedItem);
    if (!term)
        return;
    QMenu menu(tabs);
    menu.addAction(qssh::themeIcon({"view-refresh"}, QStyle::SP_BrowserReload), tr("Reconnect"),
                   this, [term]() { emit term->reconnect(); });
    QAction *disc = menu.addAction(qssh::themeIcon({"process-stop"}, QStyle::SP_BrowserStop), tr("Disconnect"),
                                   this, [term]() {
                                       emit term->disconnectSession();
                                       term->sessionError(0);
                                       term->writeData("\r\nDisconnected. Press Enter to reconnect ...\r\n");
                                   });
    disc->setEnabled(term->isConnected());
    menu.addAction(qssh::themeIcon({"tab-new", "list-add"}, QStyle::SP_FileDialogNewFolder), tr("Duplicate Session"),
                   this, [this, clickedItem]() { openSession(clickedItem); });
    menu.addAction(qssh::themeIcon({"folder-remote"}, QStyle::SP_DriveNetIcon), tr("SFTP File Manager..."),
                   this, [this, term]() {
                       (new QSSHTerm_Sftp_Dialog(term->siteInfo, this))->show();
                   });
    menu.addSeparator();
    menu.addAction(qssh::themeIcon({"window-close"}, QStyle::SP_DialogCloseButton), tr("Close"),
                   this, [this, term]() { closeTab(tabs->indexOf(term)); });
    QAction *others = menu.addAction(tr("Close Other Tabs"), this, [this, term]() {
        for (int i = tabs->count() - 1; i >= 0; --i)
            if (termAt(i) != term)
                closeTab(i);
    });
    others->setEnabled(tabs->count() > 1);
    menu.exec(tabs->tabBar()->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------
// dialogs

void qsshTabTerm::showStartupDialog() {
    QSettings settings(settingsPath(), QSettings::IniFormat);
    if (settings.value("connectDialog/showOnStartup", true).toBool())
        showConnectDialog();
}

void qsshTabTerm::showConnectDialog() {
    if (!connectDialog) {
        connectDialog = new QSiteTreeDialog(this);
        connectDialog->setModal(true);
        connect(connectDialog, &QSiteTreeDialog::notifyNewSession, this,
                qOverload<SiteInfo>(&qsshTabTerm::openSession));
    }
    connectDialog->show();
    connectDialog->raise();
    connectDialog->activateWindow();
}

void qsshTabTerm::showQuickConnectDialog() {
    SiteInfo info;
    if (SiteTree::editSiteInfo(this, tr("Quick Connect"), info))
        openSession(info);
}

// "[user@]host[:port]" or the name of a saved session
void qsshTabTerm::quickConnectFromBox() {
    const QString text = quickConnect->text().trimmed();
    if (text.isEmpty())
        return;

    SiteInfo info;
    // a saved session with this exact name wins
    manager->tree()->findSite(text, &info);

    if (info.hostname.isEmpty()) {
        static const QRegularExpression re(QStringLiteral("^(?:([^@\\s]+)@)?([^@:\\s]+|\\[[^\\]]+\\])(?::(\\d{1,5}))?$"));
        const QRegularExpressionMatch m = re.match(text);
        if (!m.hasMatch()) {
            statusMessage->setText(tr("Invalid host \"%1\": use [user@]host[:port]").arg(text));
            return;
        }
        info.username = m.captured(1);
        info.hostname = m.captured(2).remove('[').remove(']');
        info.port = m.captured(3).isEmpty() ? 22 : m.captured(3).toInt();
        info.sitename = info.hostname;
    }

    QSettings settings(settingsPath(), QSettings::IniFormat);
    QStringList history = settings.value("quickConnect/history").toStringList();
    history.removeAll(text);
    history.prepend(text);
    while (history.size() > 20)
        history.removeLast();
    settings.setValue("quickConnect/history", history);
    if (auto *model = qobject_cast<QStringListModel *>(quickConnect->completer()->model()))
        model->setStringList(history);

    quickConnect->clear();
    openSession(info);
}

void qsshTabTerm::showSftpMgrDialog() {
    QSSHTerm *term = currentTerm();
    if (!term)
        return;
    // one non-modal window per request, so transfers can run while the
    // terminal is in use; it deletes itself when closed
    QSSHTerm_Sftp_Dialog *dialog = new QSSHTerm_Sftp_Dialog(term->siteInfo, this);
    dialog->show();
}

void qsshTabTerm::showFindDialog() {
    if (QSSHTerm *term = currentTerm())
        term->toggleShowSearchBar();
}

void qsshTabTerm::showAboutDialog() {
    QMessageBox::about(this, tr("About QSSHTerm"),
                       tr("<h3>QSSHTerm 0.2</h3>"
                          "<p>A tabbed SSH terminal with session manager and SFTP.</p>"
                          "<p>Author: David Fan<br>License: LGPL</p>"
                          "<p>Built with Qt %1 and libssh %2.</p>")
                           .arg(QString::fromLatin1(qVersion()), QString::fromLatin1(ssh_version(0))));
}

void qsshTabTerm::closeEvent(QCloseEvent *event) {
    int live = 0;
    for (int i = 0; i < tabs->count(); ++i)
        if (QSSHTerm *t = termAt(i); t && t->isConnected())
            ++live;
    if (live > 0 &&
        QMessageBox::question(this, tr("Exit"),
                              tr("%n session(s) still connected. Disconnect and exit?", "", live))
            != QMessageBox::Yes) {
        event->ignore();
        return;
    }
    QSettings settings(settingsPath(), QSettings::IniFormat);
    settings.setValue("mainWindow/geometry", saveGeometry());
    settings.setValue("mainWindow/state", saveState());
    event->accept();
}

// ---------------------------------------------------------------------------
// sessions

void qsshTabTerm::openSession(SiteInfo info) {
    QString labelTitle = info.sitename.isEmpty() ? info.hostname : info.sitename;
    int maxIndex = 0;
    for (int j = 0; j < tabs->count(); j++) {
        QString title = tabs->tabText(j);
        if (!title.startsWith(labelTitle))
            continue;
        if (title == labelTitle && maxIndex == 0) {
            maxIndex = 1;
            continue;
        }
        QString possibleNumberPair = title.mid(labelTitle.length());
        if (possibleNumberPair.startsWith("(") && possibleNumberPair.endsWith(")")) {
            bool ok;
            int tabNumber = possibleNumberPair.mid(1, possibleNumberPair.length() - 2).toInt(&ok, 10);
            if (ok && maxIndex <= tabNumber)
                maxIndex = tabNumber + 1;
        }
    }
    if (maxIndex > 0)
        labelTitle = labelTitle + "(" + QString::number(maxIndex) + ")";

    QSSHTerm *term = new QSSHTerm(info);
    term->setTabTermKey(labelTitle);
    // connect before start() so no state change is missed
    connect(term, &QSSHTerm::icon_change, this, &qsshTabTerm::changeTabIcon);
    connect(term, &QSSHTerm::savePasswordRequested, this, [this](const QString &site, const QString &password) {
        if (!SiteTree::storePassword(site, password))
            QMessageBox::warning(this, tr("Save Password"), tr("Cannot save the password of %1.").arg(site));
    });
    connect(term, &QSSHTerm::statusChanged, this, [this, term]() {
        changeTabIcon(term->getTermKey(), term->state());
        if (term == currentTerm())
            updateStatus();
    });
    connect(term, &QSSHTerm::resizeSshWindow, this, [this, term]() {
        if (term == currentTerm())
            updateStatus();
    });

    int idx = tabs->addTab(term, labelTitle);
    tabs->setTabToolTip(idx, siteAddress(info));
    tabs->tabBar()->setTabData(idx, int(StatusIcon::Connecting));
    tabs->setTabIcon(idx, qssh::statusIcon(StatusIcon::Connecting));
    tabs->setCurrentIndex(idx);
    term->start();
    term->setFocus();
    updateSessionActions();
    updateStatus();
}

void qsshTabTerm::openSession(int index) {
     if (QSSHTerm * term = termAt(index))
         this->openSession(term->siteInfo);
}

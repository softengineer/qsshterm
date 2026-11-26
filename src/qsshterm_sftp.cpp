#include "qsshterm_sftp.h"
#include "qssh_helpers.h"
#include "qsiteconfig.h"

#include <fcntl.h>
#include <sys/stat.h>

// Number of read/write requests kept in flight per transfer. Pipelining hides
// the round trip, which otherwise caps throughput at chunk / RTT.
static constexpr int kQueueDepth = 16;
static constexpr size_t kMaxChunk = 256 * 1024;
static constexpr int kProgressIntervalMs = 100;

enum ItemRole {
    RoleName = Qt::UserRole,
    RoleIsDir,
    RoleIsLink,
    RoleIsParent
};

// ---------------------------------------------------------------------------
// helpers

static QFileDevice::Permissions toQtPermissions(quint32 m)
{
    QFileDevice::Permissions p;
    if (m & 0400) p |= QFileDevice::ReadOwner | QFileDevice::ReadUser;
    if (m & 0200) p |= QFileDevice::WriteOwner | QFileDevice::WriteUser;
    if (m & 0100) p |= QFileDevice::ExeOwner | QFileDevice::ExeUser;
    if (m & 0040) p |= QFileDevice::ReadGroup;
    if (m & 0020) p |= QFileDevice::WriteGroup;
    if (m & 0010) p |= QFileDevice::ExeGroup;
    if (m & 0004) p |= QFileDevice::ReadOther;
    if (m & 0002) p |= QFileDevice::WriteOther;
    if (m & 0001) p |= QFileDevice::ExeOther;
    return p;
}

static quint32 toUnixPermissions(QFileDevice::Permissions p)
{
    quint32 m = 0;
    if (p & QFileDevice::ReadOwner)  m |= 0400;
    if (p & QFileDevice::WriteOwner) m |= 0200;
    if (p & QFileDevice::ExeOwner)   m |= 0100;
    if (p & QFileDevice::ReadGroup)  m |= 0040;
    if (p & QFileDevice::WriteGroup) m |= 0020;
    if (p & QFileDevice::ExeGroup)   m |= 0010;
    if (p & QFileDevice::ReadOther)  m |= 0004;
    if (p & QFileDevice::WriteOther) m |= 0002;
    if (p & QFileDevice::ExeOther)   m |= 0001;
    return m;
}

static QString permissionString(quint32 m, bool isDir, bool isLink)
{
    QString s = isLink ? QStringLiteral("l") : isDir ? QStringLiteral("d") : QStringLiteral("-");
    static const char rwx[] = "rwxrwxrwx";
    for (int i = 0; i < 9; ++i)
        s += (m & (0400u >> i)) ? QChar(rwx[i]) : QChar('-');
    return s;
}

static QString remoteBaseName(const QString &path)
{
    QString p = path;
    while (p.size() > 1 && p.endsWith('/'))
        p.chop(1);
    return p.section('/', -1);
}

static QString remoteJoinPath(const QString &dir, const QString &name)
{
    return dir.endsWith('/') ? dir + name : dir + '/' + name;
}

static QString remoteParent(const QString &dir)
{
    if (dir.isEmpty() || dir == "/")
        return QStringLiteral("/");
    QString p = dir;
    while (p.size() > 1 && p.endsWith('/'))
        p.chop(1);
    const int slash = p.lastIndexOf('/');
    return slash <= 0 ? QStringLiteral("/") : p.left(slash);
}

static QIcon themedIcon(const char *name, QStyle::StandardPixmap fallback)
{
    QIcon icon = QIcon::fromTheme(QString::fromLatin1(name));
    return icon.isNull() ? QApplication::style()->standardIcon(fallback) : icon;
}

// ---------------------------------------------------------------------------
// dialog

QSSHTerm_Sftp_Dialog::QSSHTerm_Sftp_Dialog(const SiteInfo &info, QWidget *parent)
    : QDialog(parent), siteInfo(info)
{
    SiteTree::resolveJumpHosts(siteInfo);
    qRegisterMetaType<SftpEntry>();
    qRegisterMetaType<QList<SftpEntry>>();

    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::WindowMaximizeButtonHint);
    initGui();
    listLocal(QDir::homePath());

    cancelGen = new std::atomic<int>(0);
    session = new QSSHTerm_Sftp_Session(siteInfo, cancelGen);
    // no parent: the thread must outlive this dialog until the session has closed
    QThread *thread = new QThread();
    session->moveToThread(thread);

    connect(thread, &QThread::started, session, &QSSHTerm_Sftp_Session::connect_to);
    connect(session, &QSSHTerm_Sftp_Session::finished, thread, &QThread::quit);
    connect(session, &QSSHTerm_Sftp_Session::finished, session, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);

    connect(this, &QSSHTerm_Sftp_Dialog::req_connect, session, &QSSHTerm_Sftp_Session::connect_to);
    connect(this, &QSSHTerm_Sftp_Dialog::req_list, session, &QSSHTerm_Sftp_Session::list);
    connect(this, &QSSHTerm_Sftp_Dialog::req_mkdir, session, &QSSHTerm_Sftp_Session::mkdir);
    connect(this, &QSSHTerm_Sftp_Dialog::req_remove, session, &QSSHTerm_Sftp_Session::remove);
    connect(this, &QSSHTerm_Sftp_Dialog::req_rename, session, &QSSHTerm_Sftp_Session::rename);
    connect(this, &QSSHTerm_Sftp_Dialog::req_download, session, &QSSHTerm_Sftp_Session::download);
    connect(this, &QSSHTerm_Sftp_Dialog::req_upload, session, &QSSHTerm_Sftp_Session::upload);

    connect(session, &QSSHTerm_Sftp_Session::connected, this, &QSSHTerm_Sftp_Dialog::onConnected);
    connect(session, &QSSHTerm_Sftp_Session::listing, this, &QSSHTerm_Sftp_Dialog::onListing);
    connect(session, &QSSHTerm_Sftp_Session::progress, this, &QSSHTerm_Sftp_Dialog::onProgress);
    connect(session, &QSSHTerm_Sftp_Session::jobFinished, this, &QSSHTerm_Sftp_Dialog::onJobFinished);
    connect(session, &QSSHTerm_Sftp_Session::message, this, &QSSHTerm_Sftp_Dialog::log);

    log(tr("Connecting to %1@%2:%3 ...")
            .arg(siteInfo.username, siteInfo.hostname)
            .arg(siteInfo.port > 0 ? siteInfo.port : 22));
    status->setText(tr("Connecting ..."));
    remotePane->setEnabled(false);
    thread->start();
}

QSSHTerm_Sftp_Dialog::~QSSHTerm_Sftp_Dialog()
{
    if (session) {
        // abort running transfers, then let the session shut down in its own thread
        cancelGen->fetch_add(1);
        QObject::disconnect(session, nullptr, this, nullptr);
        QMetaObject::invokeMethod(session, &QSSHTerm_Sftp_Session::close, Qt::QueuedConnection);
    }
}

void QSSHTerm_Sftp_Dialog::initGui()
{
    const QString who = siteInfo.username.isEmpty()
        ? siteInfo.hostname : siteInfo.username + '@' + siteInfo.hostname;
    setWindowTitle(tr("SFTP - %1").arg(siteInfo.sitename.isEmpty() ? who : siteInfo.sitename));
    resize(1100, 700);

    QWidget *localPane = makePane(tr("Local"), &localPath, &localList, false);
    remotePane = makePane(tr("Remote: %1").arg(who), &remotePath, &remoteList, true);

    QSplitter *panes = new QSplitter(Qt::Horizontal);
    panes->addWidget(localPane);
    panes->addWidget(remotePane);
    panes->setChildrenCollapsible(false);

    console = new QPlainTextEdit;
    console->setReadOnly(true);
    console->setMaximumBlockCount(2000);

    progress = new QProgressBar;
    progress->setRange(0, 1000);
    progress->setTextVisible(false);
    progress->setVisible(false);
    status = new QLabel;
    cancelButton = new QPushButton(themedIcon("process-stop", QStyle::SP_BrowserStop), tr("Cancel"));
    cancelButton->setToolTip(tr("Cancel the running and queued transfers"));
    cancelButton->setEnabled(false);
    connect(cancelButton, &QPushButton::clicked, this, &QSSHTerm_Sftp_Dialog::cancelTransfers);

    QHBoxLayout *statusRow = new QHBoxLayout;
    statusRow->addWidget(status, 1);
    statusRow->addWidget(progress, 1);
    statusRow->addWidget(cancelButton);

    QWidget *bottom = new QWidget;
    QVBoxLayout *bottomLayout = new QVBoxLayout(bottom);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->addLayout(statusRow);
    bottomLayout->addWidget(console);

    QSplitter *vertical = new QSplitter(Qt::Vertical);
    vertical->addWidget(panes);
    vertical->addWidget(bottom);
    vertical->setStretchFactor(0, 4);
    vertical->setStretchFactor(1, 1);
    vertical->setChildrenCollapsible(false);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(vertical);
}

QTreeWidget *QSSHTerm_Sftp_Dialog::makeList()
{
    QTreeWidget *list = new QTreeWidget;
    list->setHeaderLabels({tr("Name"), tr("Size"), tr("Modified"), tr("Permissions")});
    list->setRootIsDecorated(false);
    list->setUniformRowHeights(true);
    list->setAllColumnsShowFocus(true);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    list->header()->setStretchLastSection(false);
    list->header()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    list->header()->setSectionResizeMode(ColSize, QHeaderView::ResizeToContents);
    list->header()->setSectionResizeMode(ColModified, QHeaderView::ResizeToContents);
    list->header()->setSectionResizeMode(ColPerms, QHeaderView::ResizeToContents);
    return list;
}

QWidget *QSSHTerm_Sftp_Dialog::makePane(const QString &title, QLineEdit **pathEdit,
                                        QTreeWidget **listOut, bool remote)
{
    QWidget *pane = new QWidget;
    QVBoxLayout *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);

    QLabel *label = new QLabel(QStringLiteral("<b>%1</b>").arg(title.toHtmlEscaped()));

    QToolBar *bar = new QToolBar;
    bar->setIconSize(QSize(18, 18));
    QAction *up = bar->addAction(themedIcon("go-up", QStyle::SP_FileDialogToParent), tr("Up"));
    up->setToolTip(tr("Parent folder (Backspace)"));
    QAction *home = bar->addAction(themedIcon("go-home", QStyle::SP_DirHomeIcon), tr("Home"));
    home->setToolTip(tr("Home folder"));
    QAction *refresh = bar->addAction(themedIcon("view-refresh", QStyle::SP_BrowserReload), tr("Refresh"));
    refresh->setToolTip(tr("Refresh (F5)"));
    QAction *mkdirAct = bar->addAction(themedIcon("folder-new", QStyle::SP_FileDialogNewFolder), tr("New Folder"));
    mkdirAct->setToolTip(tr("New folder"));
    bar->addSeparator();
    QAction *transfer = remote
        ? bar->addAction(themedIcon("go-previous", QStyle::SP_ArrowLeft), tr("Download"))
        : bar->addAction(themedIcon("go-next", QStyle::SP_ArrowRight), tr("Upload"));
    transfer->setToolTip(remote ? tr("Download selected to the local folder")
                                : tr("Upload selected to the remote folder"));
    QWidget *spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);
    QAction *del = bar->addAction(themedIcon("edit-delete", QStyle::SP_TrashIcon), tr("Delete"));
    del->setToolTip(tr("Delete selected (Delete)"));

    QLineEdit *path = new QLineEdit;
    path->setClearButtonEnabled(true);
    path->setPlaceholderText(remote ? tr("Remote path") : tr("Local path"));

    QTreeWidget *list = makeList();

    layout->addWidget(label);
    layout->addWidget(bar);
    layout->addWidget(path);
    layout->addWidget(list);

    auto shortcut = [list](const QKeySequence &key, auto slot) {
        QShortcut *s = new QShortcut(key, list);
        s->setContext(Qt::WidgetShortcut);
        QObject::connect(s, &QShortcut::activated, list, slot);
    };

    if (remote) {
        connect(up, &QAction::triggered, this, [this]() { listRemote(remoteParent(remoteDir)); });
        connect(home, &QAction::triggered, this, [this]() { listRemote(remoteHome.isEmpty() ? "." : remoteHome); });
        connect(refresh, &QAction::triggered, this, [this]() { listRemote(remoteDir.isEmpty() ? "." : remoteDir); });
        connect(mkdirAct, &QAction::triggered, this, &QSSHTerm_Sftp_Dialog::mkdirRemote);
        connect(transfer, &QAction::triggered, this, &QSSHTerm_Sftp_Dialog::downloadSelected);
        connect(del, &QAction::triggered, this, &QSSHTerm_Sftp_Dialog::deleteRemote);
        connect(path, &QLineEdit::returnPressed, this, [this, path]() { listRemote(path->text().trimmed()); });
        connect(list, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *i) { remoteActivated(i); });
        connect(list, &QWidget::customContextMenuRequested, this, &QSSHTerm_Sftp_Dialog::remoteContextMenu);
        shortcut(QKeySequence::Delete, [this]() { deleteRemote(); });
        shortcut(QKeySequence(Qt::Key_F2), [this]() { renameRemote(); });
        shortcut(QKeySequence(Qt::Key_F5), [this]() { listRemote(remoteDir.isEmpty() ? "." : remoteDir); });
        shortcut(QKeySequence(Qt::Key_Backspace), [this]() { listRemote(remoteParent(remoteDir)); });
    } else {
        connect(up, &QAction::triggered, this, [this]() {
            QDir d(localDir);
            if (d.cdUp())
                listLocal(d.absolutePath());
        });
        connect(home, &QAction::triggered, this, [this]() { listLocal(QDir::homePath()); });
        connect(refresh, &QAction::triggered, this, [this]() { listLocal(localDir); });
        connect(mkdirAct, &QAction::triggered, this, &QSSHTerm_Sftp_Dialog::mkdirLocal);
        connect(transfer, &QAction::triggered, this, &QSSHTerm_Sftp_Dialog::uploadSelected);
        connect(del, &QAction::triggered, this, &QSSHTerm_Sftp_Dialog::deleteLocal);
        connect(path, &QLineEdit::returnPressed, this, [this, path]() {
            QString p = path->text().trimmed();
            if (p.startsWith('~'))
                p = QDir::homePath() + p.mid(1);
            listLocal(p);
        });
        connect(list, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *i) { localActivated(i); });
        connect(list, &QWidget::customContextMenuRequested, this, &QSSHTerm_Sftp_Dialog::localContextMenu);
        shortcut(QKeySequence::Delete, [this]() { deleteLocal(); });
        shortcut(QKeySequence(Qt::Key_F2), [this]() { renameLocal(); });
        shortcut(QKeySequence(Qt::Key_F5), [this]() { listLocal(localDir); });
        shortcut(QKeySequence(Qt::Key_Backspace), [this]() {
            QDir d(localDir);
            if (d.cdUp())
                listLocal(d.absolutePath());
        });
    }

    *pathEdit = path;
    *listOut = list;
    return pane;
}

void QSSHTerm_Sftp_Dialog::fillItem(QTreeWidgetItem *item, const QString &name, bool isDir,
                                    bool isLink, qint64 size, const QDateTime &mtime,
                                    const QString &perms)
{
    QStyle::StandardPixmap pix = isDir ? (isLink ? QStyle::SP_DirLinkIcon : QStyle::SP_DirIcon)
                                       : (isLink ? QStyle::SP_FileLinkIcon : QStyle::SP_FileIcon);
    item->setIcon(ColName, style()->standardIcon(pix));
    item->setText(ColName, name);
    item->setText(ColSize, isDir ? QString() : QLocale().formattedDataSize(size));
    item->setTextAlignment(ColSize, Qt::AlignRight | Qt::AlignVCenter);
    item->setText(ColModified, mtime.isValid() ? QLocale().toString(mtime, QLocale::ShortFormat) : QString());
    item->setText(ColPerms, perms);
    item->setData(0, RoleName, name);
    item->setData(0, RoleIsDir, isDir);
    item->setData(0, RoleIsLink, isLink);
    item->setData(0, RoleIsParent, false);
}

static QTreeWidgetItem *parentItem(QTreeWidget *list, QStyle *style)
{
    QTreeWidgetItem *up = new QTreeWidgetItem(list);
    up->setIcon(0, style->standardIcon(QStyle::SP_FileDialogToParent));
    up->setText(0, QStringLiteral(".."));
    up->setData(0, RoleIsParent, true);
    up->setData(0, RoleIsDir, true);
    return up;
}

void QSSHTerm_Sftp_Dialog::listLocal(const QString &dir)
{
    QDir d(dir);
    if (!d.exists() || !d.isReadable()) {
        log(tr("Cannot open local folder %1").arg(dir));
        localPath->setText(localDir);
        return;
    }
    localDir = d.absolutePath();
    localPath->setText(QDir::toNativeSeparators(localDir));

    localList->clear();
    if (!d.isRoot())
        parentItem(localList, style());

    const QFileInfoList entries = d.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &fi : entries) {
        QTreeWidgetItem *item = new QTreeWidgetItem(localList);
        fillItem(item, fi.fileName(), fi.isDir(), fi.isSymLink(), fi.size(), fi.lastModified(),
                 permissionString(toUnixPermissions(fi.permissions()), fi.isDir(), fi.isSymLink()));
    }
}

void QSSHTerm_Sftp_Dialog::listRemote(const QString &dir)
{
    if (dir.isEmpty())
        return;
    emit req_list(dir);
}

void QSSHTerm_Sftp_Dialog::onConnected(QString home)
{
    if (home.isEmpty()) {
        status->setText(tr("Not connected"));
        log(tr("Not connected. Use Refresh on the remote side to retry."));
        remotePane->setEnabled(true);
        return;
    }
    remoteHome = home;
    remotePane->setEnabled(true);
    status->setText(tr("Connected"));
    log(tr("Connected. Remote home is %1").arg(home));
    listRemote(home);
}

void QSSHTerm_Sftp_Dialog::onListing(QString dir, QList<SftpEntry> entries)
{
    remoteDir = dir;
    remotePath->setText(dir);
    if (remoteHome.isEmpty()) {
        remoteHome = dir;   // first successful listing after a failed connect
        status->setText(tr("Connected"));
    }

    std::sort(entries.begin(), entries.end(), [](const SftpEntry &a, const SftpEntry &b) {
        if (a.isDir != b.isDir)
            return a.isDir;
        return QString::compare(a.name, b.name, Qt::CaseInsensitive) < 0;
    });

    remoteList->clear();
    if (dir != "/")
        parentItem(remoteList, style());
    for (const SftpEntry &e : entries) {
        QTreeWidgetItem *item = new QTreeWidgetItem(remoteList);
        fillItem(item, e.name, e.isDir, e.isLink, e.size,
                 e.mtime > 0 ? QDateTime::fromSecsSinceEpoch(e.mtime) : QDateTime(),
                 permissionString(e.permissions, e.isDir, e.isLink));
    }
}

void QSSHTerm_Sftp_Dialog::log(QString msg)
{
    console->appendPlainText(QStringLiteral("[%1] %2")
                                 .arg(QTime::currentTime().toString("HH:mm:ss"), msg));
}

QList<QTreeWidgetItem *> QSSHTerm_Sftp_Dialog::selectedEntries(QTreeWidget *list) const
{
    QList<QTreeWidgetItem *> out;
    for (QTreeWidgetItem *item : list->selectedItems())
        if (!item->data(0, RoleIsParent).toBool())
            out << item;
    return out;
}

bool QSSHTerm_Sftp_Dialog::nameExists(QTreeWidget *list, const QString &name) const
{
    for (int i = 0; i < list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = list->topLevelItem(i);
        if (!item->data(0, RoleIsParent).toBool() && item->data(0, RoleName).toString() == name)
            return true;
    }
    return false;
}

QString QSSHTerm_Sftp_Dialog::remoteJoin(const QString &dir, const QString &name) const
{
    return remoteJoinPath(dir, name);
}

void QSSHTerm_Sftp_Dialog::startJob()
{
    ++pendingJobs;
    cancelButton->setEnabled(true);
    status->setText(tr("%n job(s) queued", "", pendingJobs));
}

void QSSHTerm_Sftp_Dialog::onProgress(QString name, qint64 done, qint64 total)
{
    progress->setVisible(true);
    progress->setValue(total > 0 ? int(done * 1000 / total) : 1000);
    QLocale loc;
    status->setText(tr("%1  %2 / %3%4")
                        .arg(name, loc.formattedDataSize(done), loc.formattedDataSize(total),
                             pendingJobs > 1 ? tr("  (+%1 queued)").arg(pendingJobs - 1) : QString()));
}

void QSSHTerm_Sftp_Dialog::onJobFinished(bool ok, QString message, bool remoteChanged, bool localChanged)
{
    if (pendingJobs > 0)
        --pendingJobs;
    log(ok ? message : tr("Error: %1").arg(message));
    if (remoteChanged && !remoteDir.isEmpty())
        listRemote(remoteDir);
    if (localChanged)
        listLocal(localDir);

    if (pendingJobs == 0) {
        progress->setVisible(false);
        cancelButton->setEnabled(false);
        status->setText(ok ? tr("Ready") : tr("Last job failed, see log"));
    } else {
        status->setText(tr("%n job(s) queued", "", pendingJobs));
    }
}

void QSSHTerm_Sftp_Dialog::cancelTransfers()
{
    cancelGen->fetch_add(1);
    log(tr("Cancelling transfers ..."));
}

void QSSHTerm_Sftp_Dialog::localActivated(QTreeWidgetItem *item)
{
    if (!item)
        return;
    if (item->data(0, RoleIsParent).toBool()) {
        QDir d(localDir);
        if (d.cdUp())
            listLocal(d.absolutePath());
    } else if (item->data(0, RoleIsDir).toBool()) {
        listLocal(QDir(localDir).filePath(item->data(0, RoleName).toString()));
    } else {
        uploadSelected();
    }
}

void QSSHTerm_Sftp_Dialog::remoteActivated(QTreeWidgetItem *item)
{
    if (!item)
        return;
    if (item->data(0, RoleIsParent).toBool())
        listRemote(remoteParent(remoteDir));
    else if (item->data(0, RoleIsDir).toBool())
        listRemote(remoteJoin(remoteDir, item->data(0, RoleName).toString()));
    else
        downloadSelected();
}

// Asks before overwriting; returns false when the whole operation is cancelled.
static bool confirmOverwrite(QWidget *parent, const QString &name, bool *yesToAll, bool *skip)
{
    *skip = false;
    if (*yesToAll)
        return true;
    auto answer = QMessageBox::question(
        parent, QObject::tr("Overwrite"),
        QObject::tr("\"%1\" already exists in the destination. Overwrite it?").arg(name),
        QMessageBox::Yes | QMessageBox::YesToAll | QMessageBox::No | QMessageBox::Cancel,
        QMessageBox::No);
    if (answer == QMessageBox::Cancel)
        return false;
    if (answer == QMessageBox::YesToAll)
        *yesToAll = true;
    if (answer == QMessageBox::No)
        *skip = true;
    return true;
}

void QSSHTerm_Sftp_Dialog::uploadSelected()
{
    if (remoteDir.isEmpty()) {
        log(tr("Not connected"));
        return;
    }
    bool yesToAll = false, skip = false;
    for (QTreeWidgetItem *item : selectedEntries(localList)) {
        const QString name = item->data(0, RoleName).toString();
        if (nameExists(remoteList, name)) {
            if (!confirmOverwrite(this, name, &yesToAll, &skip))
                return;
            if (skip)
                continue;
        }
        startJob();
        emit req_upload(QDir(localDir).filePath(name), remoteDir, cancelGen->load());
    }
}

void QSSHTerm_Sftp_Dialog::downloadSelected()
{
    bool yesToAll = false, skip = false;
    for (QTreeWidgetItem *item : selectedEntries(remoteList)) {
        const QString name = item->data(0, RoleName).toString();
        if (nameExists(localList, name)) {
            if (!confirmOverwrite(this, name, &yesToAll, &skip))
                return;
            if (skip)
                continue;
        }
        startJob();
        emit req_download(remoteJoin(remoteDir, name), localDir, cancelGen->load());
    }
}

static QString describeSelection(const QList<QTreeWidgetItem *> &items)
{
    if (items.size() == 1)
        return QStringLiteral("\"%1\"").arg(items.first()->data(0, RoleName).toString());
    return QObject::tr("%1 items").arg(items.size());
}

void QSSHTerm_Sftp_Dialog::deleteLocal()
{
    const auto items = selectedEntries(localList);
    if (items.isEmpty())
        return;
    if (QMessageBox::warning(this, tr("Delete"),
                             tr("Permanently delete %1 from the local disk?\nFolders are deleted with all their contents.")
                                 .arg(describeSelection(items)),
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    for (QTreeWidgetItem *item : items) {
        const QString path = QDir(localDir).filePath(item->data(0, RoleName).toString());
        QFileInfo fi(path);
        // never recurse through a symlink: remove the link itself
        const bool ok = (fi.isDir() && !fi.isSymLink()) ? QDir(path).removeRecursively()
                                                        : QFile::remove(path);
        log(ok ? tr("Deleted %1").arg(path) : tr("Error: cannot delete %1").arg(path));
    }
    listLocal(localDir);
}

void QSSHTerm_Sftp_Dialog::deleteRemote()
{
    const auto items = selectedEntries(remoteList);
    if (items.isEmpty())
        return;
    if (QMessageBox::warning(this, tr("Delete"),
                             tr("Permanently delete %1 on %2?\nFolders are deleted with all their contents.")
                                 .arg(describeSelection(items), siteInfo.hostname),
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;
    for (QTreeWidgetItem *item : items) {
        startJob();
        // symlinks are unlinked, never followed
        const bool isDir = item->data(0, RoleIsDir).toBool() && !item->data(0, RoleIsLink).toBool();
        emit req_remove(remoteJoin(remoteDir, item->data(0, RoleName).toString()), isDir,
                        cancelGen->load());
    }
}

static bool validName(const QString &name)
{
    return !name.isEmpty() && name != "." && name != ".." && !name.contains('/');
}

void QSSHTerm_Sftp_Dialog::renameLocal()
{
    const auto items = selectedEntries(localList);
    if (items.size() != 1)
        return;
    const QString old = items.first()->data(0, RoleName).toString();
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename"), tr("New name:"),
                                               QLineEdit::Normal, old, &ok).trimmed();
    if (!ok || name == old)
        return;
    if (!validName(name)) {
        log(tr("Error: invalid name \"%1\"").arg(name));
        return;
    }
    QDir d(localDir);
    if (d.rename(old, name))
        log(tr("Renamed %1 to %2").arg(old, name));
    else
        log(tr("Error: cannot rename %1 to %2").arg(old, name));
    listLocal(localDir);
}

void QSSHTerm_Sftp_Dialog::renameRemote()
{
    const auto items = selectedEntries(remoteList);
    if (items.size() != 1)
        return;
    const QString old = items.first()->data(0, RoleName).toString();
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename"), tr("New name:"),
                                               QLineEdit::Normal, old, &ok).trimmed();
    if (!ok || name == old)
        return;
    if (!validName(name)) {
        log(tr("Error: invalid name \"%1\"").arg(name));
        return;
    }
    startJob();
    emit req_rename(remoteJoin(remoteDir, old), remoteJoin(remoteDir, name));
}

void QSSHTerm_Sftp_Dialog::mkdirLocal()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Folder"), tr("Folder name:"),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok)
        return;
    if (!validName(name)) {
        log(tr("Error: invalid name \"%1\"").arg(name));
        return;
    }
    if (QDir(localDir).mkdir(name))
        log(tr("Created %1").arg(QDir(localDir).filePath(name)));
    else
        log(tr("Error: cannot create %1").arg(name));
    listLocal(localDir);
}

void QSSHTerm_Sftp_Dialog::mkdirRemote()
{
    if (remoteDir.isEmpty())
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("New Folder"), tr("Folder name:"),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok)
        return;
    if (!validName(name)) {
        log(tr("Error: invalid name \"%1\"").arg(name));
        return;
    }
    startJob();
    emit req_mkdir(remoteJoin(remoteDir, name));
}

void QSSHTerm_Sftp_Dialog::localContextMenu(const QPoint &pos)
{
    const bool hasSel = !selectedEntries(localList).isEmpty();
    QMenu menu;
    QAction *upload = menu.addAction(themedIcon("go-next", QStyle::SP_ArrowRight), tr("Upload"));
    upload->setEnabled(hasSel && !remoteDir.isEmpty());
    connect(upload, &QAction::triggered, this, &QSSHTerm_Sftp_Dialog::uploadSelected);
    menu.addSeparator();
    QAction *rename = menu.addAction(tr("Rename"));
    rename->setShortcut(Qt::Key_F2);
    rename->setEnabled(selectedEntries(localList).size() == 1);
    connect(rename, &QAction::triggered, this, &QSSHTerm_Sftp_Dialog::renameLocal);
    QAction *del = menu.addAction(themedIcon("edit-delete", QStyle::SP_TrashIcon), tr("Delete"));
    del->setShortcut(QKeySequence::Delete);
    del->setEnabled(hasSel);
    connect(del, &QAction::triggered, this, &QSSHTerm_Sftp_Dialog::deleteLocal);
    menu.addSeparator();
    connect(menu.addAction(themedIcon("folder-new", QStyle::SP_FileDialogNewFolder), tr("New Folder")),
            &QAction::triggered, this, &QSSHTerm_Sftp_Dialog::mkdirLocal);
    connect(menu.addAction(themedIcon("view-refresh", QStyle::SP_BrowserReload), tr("Refresh")),
            &QAction::triggered, this, [this]() { listLocal(localDir); });
    menu.exec(localList->viewport()->mapToGlobal(pos));
}

void QSSHTerm_Sftp_Dialog::remoteContextMenu(const QPoint &pos)
{
    const auto items = selectedEntries(remoteList);
    QMenu menu;
    QAction *download = menu.addAction(themedIcon("go-previous", QStyle::SP_ArrowLeft), tr("Download"));
    download->setEnabled(!items.isEmpty());
    connect(download, &QAction::triggered, this, &QSSHTerm_Sftp_Dialog::downloadSelected);
    menu.addSeparator();
    QAction *rename = menu.addAction(tr("Rename"));
    rename->setShortcut(Qt::Key_F2);
    rename->setEnabled(items.size() == 1);
    connect(rename, &QAction::triggered, this, &QSSHTerm_Sftp_Dialog::renameRemote);
    QAction *del = menu.addAction(themedIcon("edit-delete", QStyle::SP_TrashIcon), tr("Delete"));
    del->setShortcut(QKeySequence::Delete);
    del->setEnabled(!items.isEmpty());
    connect(del, &QAction::triggered, this, &QSSHTerm_Sftp_Dialog::deleteRemote);
    QAction *copyPath = menu.addAction(themedIcon("edit-copy", QStyle::SP_FileIcon), tr("Copy Path"));
    copyPath->setEnabled(items.size() == 1);
    connect(copyPath, &QAction::triggered, this, [this, items]() {
        QGuiApplication::clipboard()->setText(remoteJoin(remoteDir, items.first()->data(0, RoleName).toString()));
    });
    menu.addSeparator();
    connect(menu.addAction(themedIcon("folder-new", QStyle::SP_FileDialogNewFolder), tr("New Folder")),
            &QAction::triggered, this, &QSSHTerm_Sftp_Dialog::mkdirRemote);
    connect(menu.addAction(themedIcon("view-refresh", QStyle::SP_BrowserReload), tr("Refresh")),
            &QAction::triggered, this, [this]() { listRemote(remoteDir.isEmpty() ? "." : remoteDir); });
    menu.exec(remoteList->viewport()->mapToGlobal(pos));
}

// ---------------------------------------------------------------------------
// worker

QSSHTerm_Sftp_Session::~QSSHTerm_Sftp_Session()
{
    disconnectSession();
    delete cancelGen;
}

void QSSHTerm_Sftp_Session::close()
{
    disconnectSession();
    emit finished();
}

void QSSHTerm_Sftp_Session::disconnectSession()
{
    if (sftp) {
        sftp_free(sftp);
        sftp = nullptr;
    }
    if (session) {
        connection->close();
        session = nullptr;
    }
}

QString QSSHTerm_Sftp_Session::lastError() const
{
    if (sftp) {
        switch (sftp_get_error(sftp)) {
        case SSH_FX_NO_SUCH_FILE:
        case SSH_FX_NO_SUCH_PATH:        return QStringLiteral("No such file or directory");
        case SSH_FX_PERMISSION_DENIED:   return QStringLiteral("Permission denied");
        case SSH_FX_FILE_ALREADY_EXISTS: return QStringLiteral("File already exists");
        case SSH_FX_WRITE_PROTECT:       return QStringLiteral("Write protected");
        case SSH_FX_OP_UNSUPPORTED:      return QStringLiteral("Operation not supported by server");
        case SSH_FX_FAILURE:             return QStringLiteral("Operation failed (e.g. directory not empty or already exists)");
        default: break;
        }
    }
    if (session) {
        const char *e = ssh_get_error(session);
        if (e && *e)
            return QString::fromUtf8(e);
    }
    return QStringLiteral("Unknown error");
}

bool QSSHTerm_Sftp_Session::ensureConnected()
{
    if (session && sftp && ssh_is_connected(session))
        return true;
    disconnectSession();

    if (!connection) {
        connection = std::make_unique<qssh::Connection>(siteInfo);
        connection->notice = [this](const QString &text) { emit message(text); };
    }
    QString error;
    if (!connection->open(&error)) {
        emit message(error);
        return false;
    }
    session = connection->session();

    sftp = sftp_new(session);
    if (!sftp || sftp_init(sftp) != SSH_OK) {
        emit message(QStringLiteral("Unable to start SFTP: %1").arg(QString::fromUtf8(ssh_get_error(session))));
        disconnectSession();
        return false;
    }

    readChunk = writeChunk = 32768;
    if (sftp_limits_t limits = sftp_limits(sftp)) {
        if (limits->max_read_length > 0)
            readChunk = std::min<size_t>(limits->max_read_length, kMaxChunk);
        if (limits->max_write_length > 0)
            writeChunk = std::min<size_t>(limits->max_write_length, kMaxChunk);
        sftp_limits_free(limits);
    }
    buffer.resize(qsizetype(std::max(readChunk, writeChunk)));
    return true;
}

QSSHTerm_Sftp_Session::QSSHTerm_Sftp_Session(const SiteInfo &info, std::atomic<int> *cancelGen)
    : siteInfo(info), cancelGen(cancelGen)
{
    // SFTP has no use for a 30 s+ hang on a dead host
    if (siteInfo.connectTimeout <= 0)
        siteInfo.connectTimeout = 30;
}

void QSSHTerm_Sftp_Session::connect_to()
{
    QString home;
    if (ensureConnected()) {
        QByteArray start = siteInfo.sftpStartDir.trimmed().toUtf8();
        char *p = start.isEmpty() ? nullptr : sftp_canonicalize_path(sftp, start.constData());
        if (!start.isEmpty() && !p)
            emit message(QStringLiteral("Start folder %1 not found: %2").arg(siteInfo.sftpStartDir, lastError()));
        if (!p)
            p = sftp_canonicalize_path(sftp, ".");
        if (p) {
            home = QString::fromUtf8(p);
            ssh_string_free_char(p);
        } else {
            home = QStringLiteral("/");
        }
    }
    emit connected(home);
}

void QSSHTerm_Sftp_Session::list(QString dir)
{
    if (!ensureConnected())
        return;

    QByteArray path = dir.toUtf8();
    if (char *p = sftp_canonicalize_path(sftp, path.constData())) {
        path = QByteArray(p);
        ssh_string_free_char(p);
    }

    sftp_dir d = sftp_opendir(sftp, path.constData());
    if (!d) {
        emit message(QStringLiteral("Error: cannot open %1: %2").arg(dir, lastError()));
        if (!ssh_is_connected(session))
            disconnectSession();   // reconnect on the next request
        return;
    }

    const QString dirName = QString::fromUtf8(path);
    QList<SftpEntry> entries;
    while (sftp_attributes a = sftp_readdir(sftp, d)) {
        SftpEntry e;
        e.name = QString::fromUtf8(a->name);
        if (e.name == "." || e.name == "..") {
            sftp_attributes_free(a);
            continue;
        }
        e.size = qint64(a->size);
        e.permissions = a->permissions & 07777;
        e.mtime = a->mtime;
        e.isDir = a->type == SSH_FILEXFER_TYPE_DIRECTORY;
        e.isLink = a->type == SSH_FILEXFER_TYPE_SYMLINK;
        if (e.isLink) {
            const QByteArray target = remoteJoinPath(dirName, e.name).toUtf8();
            if (sftp_attributes t = sftp_stat(sftp, target.constData())) {
                e.isDir = t->type == SSH_FILEXFER_TYPE_DIRECTORY;
                if (!e.isDir)
                    e.size = qint64(t->size);
                sftp_attributes_free(t);
            }
        }
        sftp_attributes_free(a);
        entries << e;
    }
    if (!sftp_dir_eof(d))
        emit message(QStringLiteral("Error: listing of %1 is incomplete: %2").arg(dirName, lastError()));
    sftp_closedir(d);

    emit listing(dirName, entries);
}

void QSSHTerm_Sftp_Session::mkdir(QString path)
{
    if (!ensureConnected()) {
        emit jobFinished(false, QStringLiteral("Not connected"), false, false);
        return;
    }
    const bool ok = sftp_mkdir(sftp, path.toUtf8().constData(), 0755) == SSH_OK;
    emit jobFinished(ok, ok ? QStringLiteral("Created %1").arg(path)
                            : QStringLiteral("Cannot create %1: %2").arg(path, lastError()),
                     true, false);
}

void QSSHTerm_Sftp_Session::rename(QString from, QString to)
{
    if (!ensureConnected()) {
        emit jobFinished(false, QStringLiteral("Not connected"), false, false);
        return;
    }
    const bool ok = sftp_rename(sftp, from.toUtf8().constData(), to.toUtf8().constData()) == SSH_OK;
    emit jobFinished(ok, ok ? QStringLiteral("Renamed %1 to %2").arg(from, to)
                            : QStringLiteral("Cannot rename %1: %2").arg(from, lastError()),
                     true, false);
}

void QSSHTerm_Sftp_Session::remove(QString path, bool isDir, int gen)
{
    jobGen = gen;
    if (cancelled()) {
        emit jobFinished(false, QStringLiteral("Cancelled: delete %1").arg(path), true, false);
        return;
    }
    if (!ensureConnected()) {
        emit jobFinished(false, QStringLiteral("Not connected"), false, false);
        return;
    }
    QString error;
    const bool ok = removeRecursive(path, isDir, &error);
    emit jobFinished(ok, ok ? QStringLiteral("Deleted %1").arg(path) : error, true, false);
}

bool QSSHTerm_Sftp_Session::removeRecursive(const QString &path, bool isDir, QString *error)
{
    if (cancelled()) {
        *error = QStringLiteral("Cancelled while deleting %1").arg(path);
        return false;
    }
    const QByteArray p = path.toUtf8();
    if (!isDir) {
        if (sftp_unlink(sftp, p.constData()) != SSH_OK) {
            *error = QStringLiteral("Cannot delete %1: %2").arg(path, lastError());
            return false;
        }
        return true;
    }

    sftp_dir d = sftp_opendir(sftp, p.constData());
    if (!d) {
        *error = QStringLiteral("Cannot open %1: %2").arg(path, lastError());
        return false;
    }
    // collect first: deleting while the directory handle is open is not portable
    QList<QPair<QString, bool>> children;
    while (sftp_attributes a = sftp_readdir(sftp, d)) {
        const QString name = QString::fromUtf8(a->name);
        if (name != "." && name != "..")
            children << qMakePair(name, a->type == SSH_FILEXFER_TYPE_DIRECTORY);
        sftp_attributes_free(a);
    }
    sftp_closedir(d);

    for (const auto &child : children)
        if (!removeRecursive(remoteJoinPath(path, child.first), child.second, error))
            return false;

    if (sftp_rmdir(sftp, p.constData()) != SSH_OK) {
        *error = QStringLiteral("Cannot remove folder %1: %2").arg(path, lastError());
        return false;
    }
    return true;
}

void QSSHTerm_Sftp_Session::download(QString remotePath, QString localDir, int gen)
{
    jobGen = gen;
    if (cancelled()) {
        emit jobFinished(false, QStringLiteral("Cancelled: download %1").arg(remotePath), false, false);
        return;
    }
    if (!ensureConnected()) {
        emit jobFinished(false, QStringLiteral("Not connected"), false, false);
        return;
    }
    QString error;
    QElapsedTimer timer;
    timer.start();
    const bool ok = downloadPath(remotePath, localDir, true, &error);
    if (!ok && !ssh_is_connected(session))
        disconnectSession();
    emit jobFinished(ok, ok ? QStringLiteral("Downloaded %1 (%2 s)").arg(remotePath).arg(timer.elapsed() / 1000.0, 0, 'f', 1)
                            : error,
                     false, true);
}

bool QSSHTerm_Sftp_Session::downloadPath(const QString &remotePath, const QString &localDir,
                                         bool followLink, QString *error)
{
    if (cancelled()) {
        *error = QStringLiteral("Cancelled: %1").arg(remotePath);
        return false;
    }
    const QByteArray p = remotePath.toUtf8();
    sftp_attributes a = followLink ? sftp_stat(sftp, p.constData()) : sftp_lstat(sftp, p.constData());
    if (!a) {
        *error = QStringLiteral("Cannot stat %1: %2").arg(remotePath, lastError());
        return false;
    }
    int type = a->type;
    qint64 size = qint64(a->size);
    quint32 perms = a->permissions & 0777;
    sftp_attributes_free(a);

    if (type == SSH_FILEXFER_TYPE_SYMLINK) {
        // inside a folder: follow links to files, but not to folders (loops)
        sftp_attributes t = sftp_stat(sftp, p.constData());
        if (!t || t->type != SSH_FILEXFER_TYPE_REGULAR) {
            if (t)
                sftp_attributes_free(t);
            emit message(QStringLiteral("Skipped link %1").arg(remotePath));
            return true;
        }
        type = t->type;
        size = qint64(t->size);
        perms = t->permissions & 0777;
        sftp_attributes_free(t);
    }

    const QString name = remoteBaseName(remotePath);
    const QString target = QDir(localDir).filePath(name);

    if (type == SSH_FILEXFER_TYPE_REGULAR)
        return downloadFile(remotePath, target, size, perms, error);

    if (type != SSH_FILEXFER_TYPE_DIRECTORY) {
        emit message(QStringLiteral("Skipped special file %1").arg(remotePath));
        return true;
    }

    if (!QDir(localDir).mkpath(name)) {
        *error = QStringLiteral("Cannot create local folder %1").arg(target);
        return false;
    }
    sftp_dir d = sftp_opendir(sftp, p.constData());
    if (!d) {
        *error = QStringLiteral("Cannot open %1: %2").arg(remotePath, lastError());
        return false;
    }
    QStringList children;
    while (sftp_attributes c = sftp_readdir(sftp, d)) {
        const QString child = QString::fromUtf8(c->name);
        if (child != "." && child != "..")
            children << child;
        sftp_attributes_free(c);
    }
    sftp_closedir(d);

    for (const QString &child : children)
        if (!downloadPath(remoteJoinPath(remotePath, child), target, false, error))
            return false;
    return true;
}

bool QSSHTerm_Sftp_Session::downloadFile(const QString &remotePath, const QString &localFile,
                                         qint64 size, quint32 perms, QString *error)
{
    sftp_file f = sftp_open(sftp, remotePath.toUtf8().constData(), O_RDONLY, 0);
    if (!f) {
        *error = QStringLiteral("Cannot open %1: %2").arg(remotePath, lastError());
        return false;
    }

    // written to a temporary file and renamed on success: a failed or
    // cancelled download never clobbers an existing local file
    QSaveFile out(localFile);
    if (!out.open(QIODevice::WriteOnly)) {
        sftp_close(f);
        *error = QStringLiteral("Cannot write %1: %2").arg(localFile, out.errorString());
        return false;
    }

    const QString name = remoteBaseName(remotePath);
    struct Request { sftp_aio aio; qint64 len; };
    QList<Request> pending;
    qint64 requested = 0, done = 0;
    bool ok = true;
    QElapsedTimer tick;
    tick.start();
    emit progress(name, 0, size);

    while (ok) {
        while (pending.size() < kQueueDepth && requested < size) {
            sftp_aio aio = nullptr;
            const size_t len = size_t(std::min<qint64>(qint64(readChunk), size - requested));
            const ssize_t n = sftp_aio_begin_read(f, len, &aio);
            if (n == SSH_ERROR) {
                *error = QStringLiteral("Read request failed for %1: %2").arg(remotePath, lastError());
                ok = false;
                break;
            }
            pending.append({aio, qint64(n)});
            requested += n;
        }
        if (!ok || pending.isEmpty())
            break;

        Request r = pending.takeFirst();
        const ssize_t n = sftp_aio_wait_read(&r.aio, buffer.data(), size_t(buffer.size()));
        if (n < 0) {
            *error = QStringLiteral("Read failed for %1: %2").arg(remotePath, lastError());
            ok = false;
            break;
        }
        if (n > 0 && out.write(buffer.constData(), n) != n) {
            *error = QStringLiteral("Cannot write %1: %2").arg(localFile, out.errorString());
            ok = false;
            break;
        }
        done += n;

        if (n < r.len) {
            // short read: discard the requests issued past it and resume from here
            for (Request &q : pending)
                sftp_aio_wait_read(&q.aio, buffer.data(), size_t(buffer.size()));
            pending.clear();
            if (n == 0)
                break;   // EOF, the file shrank while downloading
            sftp_seek64(f, quint64(done));
            requested = done;
        }

        if (tick.elapsed() >= kProgressIntervalMs) {
            tick.restart();
            emit progress(name, done, size);
        }
        if (cancelled()) {
            *error = QStringLiteral("Cancelled: %1").arg(remotePath);
            ok = false;
        }
    }

    for (Request &q : pending)
        sftp_aio_wait_read(&q.aio, buffer.data(), size_t(buffer.size()));
    sftp_close(f);

    if (!ok) {
        out.cancelWriting();
        return false;
    }
    if (!out.commit()) {
        *error = QStringLiteral("Cannot write %1: %2").arg(localFile, out.errorString());
        return false;
    }
    if (perms)
        QFile::setPermissions(localFile, toQtPermissions(perms | 0600));
    emit progress(name, done, size);
    return true;
}

void QSSHTerm_Sftp_Session::upload(QString localPath, QString remoteDir, int gen)
{
    jobGen = gen;
    if (cancelled()) {
        emit jobFinished(false, QStringLiteral("Cancelled: upload %1").arg(localPath), true, false);
        return;
    }
    if (!ensureConnected()) {
        emit jobFinished(false, QStringLiteral("Not connected"), false, false);
        return;
    }
    QString error;
    QElapsedTimer timer;
    timer.start();
    const bool ok = uploadPath(localPath, remoteDir, &error);
    if (!ok && !ssh_is_connected(session))
        disconnectSession();
    emit jobFinished(ok, ok ? QStringLiteral("Uploaded %1 (%2 s)").arg(localPath).arg(timer.elapsed() / 1000.0, 0, 'f', 1)
                            : error,
                     true, false);
}

bool QSSHTerm_Sftp_Session::uploadPath(const QString &localPath, const QString &remoteDir,
                                       QString *error)
{
    if (cancelled()) {
        *error = QStringLiteral("Cancelled: %1").arg(localPath);
        return false;
    }
    const QFileInfo fi(localPath);
    const QString remote = remoteJoinPath(remoteDir, fi.fileName());

    if (fi.isFile())
        return uploadFile(localPath, remote, error);

    if (!fi.isDir()) {
        emit message(QStringLiteral("Skipped %1").arg(localPath));
        return true;
    }

    const QByteArray r = remote.toUtf8();
    if (sftp_mkdir(sftp, r.constData(), 0755) != SSH_OK) {
        // OpenSSH reports an existing folder as a generic failure: check
        sftp_attributes a = sftp_stat(sftp, r.constData());
        const bool isDir = a && a->type == SSH_FILEXFER_TYPE_DIRECTORY;
        if (a)
            sftp_attributes_free(a);
        if (!isDir) {
            *error = QStringLiteral("Cannot create folder %1: %2").arg(remote, lastError());
            return false;
        }
    }

    const QFileInfoList children = QDir(localPath).entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QFileInfo &child : children) {
        if (child.isSymLink() && child.isDir()) {
            emit message(QStringLiteral("Skipped link %1").arg(child.filePath()));
            continue;   // avoid loops
        }
        if (!uploadPath(child.filePath(), remote, error))
            return false;
    }
    return true;
}

bool QSSHTerm_Sftp_Session::uploadFile(const QString &localFile, const QString &remoteFile,
                                       QString *error)
{
    QFile in(localFile);
    if (!in.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("Cannot read %1: %2").arg(localFile, in.errorString());
        return false;
    }
    const qint64 size = in.size();
    const mode_t mode = mode_t(toUnixPermissions(in.permissions()) | 0600);
    const QByteArray r = remoteFile.toUtf8();

    sftp_file f = sftp_open(sftp, r.constData(), O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (!f) {
        *error = QStringLiteral("Cannot create %1: %2").arg(remoteFile, lastError());
        return false;
    }

    const QString name = QFileInfo(localFile).fileName();
    QList<sftp_aio> pending;
    qint64 done = 0;
    bool ok = true;
    QElapsedTimer tick;
    tick.start();
    emit progress(name, 0, size);

    while (ok) {
        while (pending.size() < kQueueDepth && !in.atEnd()) {
            const qint64 n = in.read(buffer.data(), qint64(writeChunk));
            if (n < 0) {
                *error = QStringLiteral("Cannot read %1: %2").arg(localFile, in.errorString());
                ok = false;
                break;
            }
            if (n == 0)
                break;
            sftp_aio aio = nullptr;
            // the data is copied into the outgoing packet, so the buffer can be reused
            const ssize_t w = sftp_aio_begin_write(f, buffer.constData(), size_t(n), &aio);
            if (w == SSH_ERROR) {
                *error = QStringLiteral("Write request failed for %1: %2").arg(remoteFile, lastError());
                ok = false;
                break;
            }
            if (w < n)
                in.seek(in.pos() - (n - w));   // capped by the server limit: resend the rest
            pending.append(aio);
        }
        if (!ok || pending.isEmpty())
            break;

        sftp_aio aio = pending.takeFirst();
        const ssize_t w = sftp_aio_wait_write(&aio);
        if (w == SSH_ERROR) {
            *error = QStringLiteral("Write failed for %1: %2").arg(remoteFile, lastError());
            ok = false;
            break;
        }
        done += w;

        if (tick.elapsed() >= kProgressIntervalMs) {
            tick.restart();
            emit progress(name, done, size);
        }
        if (cancelled()) {
            *error = QStringLiteral("Cancelled: %1").arg(localFile);
            ok = false;
        }
    }

    for (sftp_aio &aio : pending)
        sftp_aio_wait_write(&aio);
    if (sftp_close(f) != SSH_OK && ok) {
        *error = QStringLiteral("Cannot finish %1: %2").arg(remoteFile, lastError());
        ok = false;
    }
    if (!ok) {
        sftp_unlink(sftp, r.constData());   // don't leave a truncated file behind
        return false;
    }
    emit progress(name, done, size);
    return true;
}

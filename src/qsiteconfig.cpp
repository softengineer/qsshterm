#include <QtWidgets>

#include "qsiteconfig.h"
#include "qssh_icons.h"
#include "qsite_editor.h"

enum { DomElementRole = Qt::UserRole + 1, DomNodeRole = Qt::UserRole + 2 };

Q_DECLARE_METATYPE(QDomElement)

static inline QString siteElement() { return QStringLiteral("site"); }
static inline QString folderElement() { return QStringLiteral("folder"); }
static inline QString hostElement() { return QStringLiteral("host"); }
static inline QString userElement() { return QStringLiteral("username"); }
static inline QString privateElement() { return QStringLiteral("privatekey"); }

static inline QString nameAttribute() { return QStringLiteral("name"); }
static inline QString ipAttribute() { return QStringLiteral("ip"); }
static inline QString portAttribute() { return QStringLiteral("port"); }
static inline QString fileAttribute() { return QStringLiteral("file"); }

// Option groups below <site>; anything else inside a site is kept untouched.
static const QStringList &managedElements()
{
    static const QStringList names = {"host", "username", "privatekey", "password", "connection",
                                      "proxy", "auth", "forwarding", "terminal", "advanced"};
    return names;
}

static int intAttr(const QDomElement &e, const QString &name, int fallback)
{
    bool ok = false;
    const int v = e.attribute(name).toInt(&ok);
    return ok ? v : fallback;
}

static bool boolAttr(const QDomElement &e, const QString &name, bool fallback)
{
    if (!e.hasAttribute(name))
        return fallback;
    const QString v = e.attribute(name);
    return v == QLatin1String("1") || v.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
}

static const char *const proxyNames[] = {"none", "socks5", "socks4", "http", "command"};
static const char *const familyNames[] = {"any", "ipv4", "ipv6"};
static const char *const hostKeyNames[] = {"ask", "accept-new", "strict", "off"};
static const char *const forwardNames[] = {"local", "remote", "dynamic"};

template <size_t N>
static int indexOf(const char *const (&names)[N], const QString &value, int fallback)
{
    for (size_t i = 0; i < N; ++i)
        if (value == QLatin1String(names[i]))
            return int(i);
    return fallback;
}

static SiteInfo siteInfoFromElement(const QDomElement &element)
{
    SiteInfo info;
    info.sitename = element.attribute(nameAttribute());
    for (QDomElement elm = element.firstChildElement(); !elm.isNull();
         elm = elm.nextSiblingElement()) {
        const QString tagName = elm.tagName();
        if (tagName == hostElement()) {
            info.hostname = elm.attribute("address");
            bool ok = false;
            const int port = elm.attribute(portAttribute()).toInt(&ok, 10);
            info.port = (ok && port > 0 && port <= 65535) ? port : -1;
        } else if (tagName == userElement()) {
            info.username = elm.text();
        } else if (tagName == privateElement()) {
            info.privateKeyPath = elm.attribute(fileAttribute());
            info.certificatePath = elm.attribute("certificate");
        } else if (tagName == "password") {
            info.password = elm.text();
        } else if (tagName == "connection") {
            info.jumpHosts = elm.attribute("jump");
            info.connectTimeout = intAttr(elm, "timeout", 0);
            info.keepAliveInterval = intAttr(elm, "keepalive", 0);
            info.addressFamily = indexOf(familyNames, elm.attribute("family"), SiteInfo::AnyFamily);
            info.bindAddress = elm.attribute("bind");
            info.useSshConfig = boolAttr(elm, "sshconfig", true);
            info.sftpStartDir = elm.attribute("sftpdir");
        } else if (tagName == "proxy") {
            info.proxyType = indexOf(proxyNames, elm.attribute("type"), SiteInfo::NoProxy);
            info.proxyHost = elm.attribute("host");
            info.proxyPort = intAttr(elm, "port", 0);
            info.proxyUser = elm.attribute("user");
            info.proxyPassword = elm.attribute("password");
            info.proxyCommand = elm.attribute("command");
        } else if (tagName == "auth") {
            info.authPublicKey = boolAttr(elm, "publickey", true);
            info.authAgent = boolAttr(elm, "agent", true);
            info.authPassword = boolAttr(elm, "password", true);
            info.authKeyboardInteractive = boolAttr(elm, "keyboard-interactive", true);
            info.authGssapi = boolAttr(elm, "gssapi", true);
            info.gssapiDelegate = boolAttr(elm, "gssapi-delegate", false);
            info.agentSocket = elm.attribute("agent-socket");
        } else if (tagName == "forwarding") {
            info.transferX11 = boolAttr(elm, "x11", false);
            info.forwardAgent = boolAttr(elm, "agent", false);
            for (QDomElement f = elm.firstChildElement("forward"); !f.isNull(); f = f.nextSiblingElement("forward")) {
                PortForward fw;
                fw.type = PortForward::Type(indexOf(forwardNames, f.attribute("type"), PortForward::Local));
                fw.bindAddress = f.attribute("bind");
                fw.bindPort = intAttr(f, "port", 0);
                fw.destHost = f.attribute("host");
                fw.destPort = intAttr(f, "hostport", 0);
                info.forwards << fw;
            }
        } else if (tagName == "terminal") {
            info.termType = elm.attribute("type");
            info.encoding = elm.attribute("encoding");
            info.colorScheme = elm.attribute("colors");
            info.fontFamily = elm.attribute("font");
            info.fontSize = intAttr(elm, "fontsize", 0);
            info.scrollback = intAttr(elm, "scrollback", -2);
            info.keyboardLayout = elm.attribute("keyboard");
            info.remoteCommand = elm.attribute("command");
            info.startupCommands = elm.firstChildElement("startup").text();
            for (QDomElement e = elm.firstChildElement("env"); !e.isNull(); e = e.nextSiblingElement("env"))
                info.environment << qMakePair(e.attribute("name"), e.text());
        } else if (tagName == "advanced") {
            info.compression = intAttr(elm, "compression", -1);
            info.ciphers = elm.attribute("ciphers");
            info.kexAlgorithms = elm.attribute("kex");
            info.macs = elm.attribute("macs");
            info.hostKeyAlgorithms = elm.attribute("hostkeys");
            info.pubkeyAcceptedTypes = elm.attribute("pubkeytypes");
            info.hostKeyCheck = indexOf(hostKeyNames, elm.attribute("hostkeycheck"), SiteInfo::HostKeyAsk);
            info.knownHostsFile = elm.attribute("knownhosts");
            info.logLevel = intAttr(elm, "loglevel", 1);
        }
    }
    return info;
}

namespace {
// Collects attributes that differ from the defaults; the element is only
// written when it has any, so plain sessions stay as short as before.
struct Group {
    QDomElement element;
    bool used = false;
    Group(QDomDocument &doc, const QString &tag) : element(doc.createElement(tag)) {}
    void text(const char *name, const QString &value)
    {
        if (!value.isEmpty()) {
            element.setAttribute(name, value);
            used = true;
        }
    }
    void number(const char *name, int value, int fallback)
    {
        if (value != fallback) {
            element.setAttribute(name, value);
            used = true;
        }
    }
    void flag(const char *name, bool value, bool fallback)
    {
        if (value != fallback) {
            element.setAttribute(name, value ? 1 : 0);
            used = true;
        }
    }
    void child(const QDomElement &e)
    {
        element.appendChild(e);
        used = true;
    }
    void addTo(QDomElement &parent) const
    {
        if (used)
            parent.appendChild(element);
    }
};
} // namespace

static void writeSiteElement(QDomDocument &doc, QDomElement &element, const SiteInfo &info)
{
    element.setAttribute(nameAttribute(), info.sitename);
    // drop the old connection settings, keep anything else untouched
    for (QDomElement elm = element.firstChildElement(); !elm.isNull();) {
        QDomElement next = elm.nextSiblingElement();
        if (managedElements().contains(elm.tagName()))
            element.removeChild(elm);
        elm = next;
    }

    QDomElement host = doc.createElement(hostElement());
    host.setAttribute("address", info.hostname);
    host.setAttribute(portAttribute(), info.port > 0 ? info.port : 22);
    element.appendChild(host);

    if (!info.username.isEmpty()) {
        QDomElement user = doc.createElement(userElement());
        user.appendChild(doc.createTextNode(info.username));
        element.appendChild(user);
    }
    if (!info.password.isEmpty()) {
        QDomElement pass = doc.createElement("password");
        pass.appendChild(doc.createTextNode(info.password));
        element.appendChild(pass);
    }
    if (!info.privateKeyPath.isEmpty() || !info.certificatePath.isEmpty()) {
        QDomElement key = doc.createElement(privateElement());
        key.setAttribute(fileAttribute(), info.privateKeyPath);
        if (!info.certificatePath.isEmpty())
            key.setAttribute("certificate", info.certificatePath);
        element.appendChild(key);
    }

    Group connection(doc, "connection");
    connection.text("jump", info.jumpHosts.trimmed());
    connection.number("timeout", info.connectTimeout, 0);
    connection.number("keepalive", info.keepAliveInterval, 0);
    if (info.addressFamily != SiteInfo::AnyFamily)
        connection.text("family", familyNames[info.addressFamily]);
    connection.text("bind", info.bindAddress);
    connection.flag("sshconfig", info.useSshConfig, true);
    connection.text("sftpdir", info.sftpStartDir);
    connection.addTo(element);

    if (info.proxyType != SiteInfo::NoProxy) {
        Group proxy(doc, "proxy");
        proxy.text("type", proxyNames[info.proxyType]);
        proxy.text("host", info.proxyHost);
        proxy.number("port", info.proxyPort, 0);
        proxy.text("user", info.proxyUser);
        proxy.text("password", info.proxyPassword);
        proxy.text("command", info.proxyCommand);
        proxy.addTo(element);
    }

    Group auth(doc, "auth");
    auth.flag("publickey", info.authPublicKey, true);
    auth.flag("agent", info.authAgent, true);
    auth.flag("password", info.authPassword, true);
    auth.flag("keyboard-interactive", info.authKeyboardInteractive, true);
    auth.flag("gssapi", info.authGssapi, true);
    auth.flag("gssapi-delegate", info.gssapiDelegate, false);
    auth.text("agent-socket", info.agentSocket);
    auth.addTo(element);

    Group forwarding(doc, "forwarding");
    forwarding.flag("x11", info.transferX11, false);
    forwarding.flag("agent", info.forwardAgent, false);
    for (const PortForward &f : info.forwards) {
        QDomElement fw = doc.createElement("forward");
        fw.setAttribute("type", forwardNames[f.type]);
        if (!f.bindAddress.isEmpty())
            fw.setAttribute("bind", f.bindAddress);
        fw.setAttribute("port", f.bindPort);
        if (f.type != PortForward::Dynamic) {
            fw.setAttribute("host", f.destHost);
            fw.setAttribute("hostport", f.destPort);
        }
        forwarding.child(fw);
    }
    forwarding.addTo(element);

    Group terminal(doc, "terminal");
    terminal.text("type", info.termType);
    terminal.text("encoding", info.encoding);
    terminal.text("colors", info.colorScheme);
    terminal.text("font", info.fontFamily);
    terminal.number("fontsize", info.fontSize, 0);
    terminal.number("scrollback", info.scrollback, -2);
    terminal.text("keyboard", info.keyboardLayout);
    terminal.text("command", info.remoteCommand);
    if (!info.startupCommands.trimmed().isEmpty()) {
        QDomElement startup = doc.createElement("startup");
        startup.appendChild(doc.createTextNode(info.startupCommands));
        terminal.child(startup);
    }
    for (const auto &var : info.environment) {
        QDomElement env = doc.createElement("env");
        env.setAttribute("name", var.first);
        env.appendChild(doc.createTextNode(var.second));
        terminal.child(env);
    }
    terminal.addTo(element);

    Group advanced(doc, "advanced");
    advanced.number("compression", info.compression, -1);
    advanced.text("ciphers", info.ciphers);
    advanced.text("kex", info.kexAlgorithms);
    advanced.text("macs", info.macs);
    advanced.text("hostkeys", info.hostKeyAlgorithms);
    advanced.text("pubkeytypes", info.pubkeyAcceptedTypes);
    if (info.hostKeyCheck != SiteInfo::HostKeyAsk)
        advanced.text("hostkeycheck", hostKeyNames[info.hostKeyCheck]);
    advanced.text("knownhosts", info.knownHostsFile);
    advanced.number("loglevel", info.logLevel, 1);
    advanced.addTo(element);
}

static QStringList allSessionNames();

static bool editSiteDialog(QWidget *parent, const QString &title, SiteInfo &info)
{
    QStringList names = allSessionNames();
    names.removeAll(info.sitename);
    return qssh::editSite(parent, title, info, names);
}

QString SiteTree::sitesFilePath()
{
    const QString dirPath = QDir::homePath() + "/.qsshterm";
    const QString path = dirPath + "/sites.xml";
    if (QFile::exists(path))
        return path;

    QDir().mkpath(dirPath);
    // seed from the config/ folder shipped next to the program (or the
    // working directory, where older versions kept it)
    for (const QString &legacy : {QCoreApplication::applicationDirPath() + "/config/sites.xml",
                                  QStringLiteral("./config/sites.xml")}) {
        if (QFile::exists(legacy) && QFile::copy(legacy, path)) {
            QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            return path;
        }
    }

    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<sites>\n</sites>\n");
        f.close();
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    return path;
}

// ---------------------------------------------------------------------------
// shared document

static QDomDocument &sharedDoc()
{
    static QDomDocument doc;
    return doc;
}

static QList<SiteTree *> &allTrees()
{
    static QList<SiteTree *> trees;
    return trees;
}

// Loads sites.xml once for all trees; on error starts from an empty list
static void ensureLoaded(QWidget *parent)
{
    static bool loaded = false;
    if (loaded)
        return;
    loaded = true;

    QDomDocument &doc = sharedDoc();
    QFile file(SiteTree::sitesFilePath());
    if (file.open(QIODevice::ReadOnly)) {
        const QDomDocument::ParseResult result = doc.setContent(&file);
        if (!result)
            QMessageBox::warning(parent, QObject::tr("Session Manager"),
                                 QObject::tr("Cannot parse %1 at line %2, column %3:\n%4")
                                     .arg(file.fileName()).arg(result.errorLine)
                                     .arg(result.errorColumn).arg(result.errorMessage));
    }
    if (doc.documentElement().tagName() != "sites") {
        doc.clear();
        doc.appendChild(doc.createProcessingInstruction("xml", "version=\"1.0\" encoding=\"UTF-8\""));
        doc.appendChild(doc.createElement("sites"));
    }
}

static bool saveShared()
{
    QSaveFile out(SiteTree::sitesFilePath());
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream stream(&out);
    sharedDoc().save(stream, 4);
    stream.flush();
    if (!out.commit())
        return false;
    QFile::setPermissions(out.fileName(), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return true;
}

static QStringList allSessionNames()
{
    ensureLoaded(nullptr);
    QStringList names;
    QDomNodeList sites = sharedDoc().elementsByTagName(siteElement());
    for (int i = 0; i < sites.size(); ++i)
        names << sites.at(i).toElement().attribute(nameAttribute());
    names.sort(Qt::CaseInsensitive);
    return names;
}

static QDomElement findSiteElement(const QString &name)
{
    ensureLoaded(nullptr);
    QDomNodeList sites = sharedDoc().elementsByTagName(siteElement());
    for (int i = 0; i < sites.size(); ++i) {
        QDomElement e = sites.at(i).toElement();
        if (e.attribute(nameAttribute()).compare(name, Qt::CaseInsensitive) == 0)
            return e;
    }
    return QDomElement();
}

void SiteTree::resolveJumpHosts(SiteInfo &info)
{
    info.jumpChain.clear();
    const QStringList hops = info.jumpHosts.split(',', Qt::SkipEmptyParts);
    for (QString spec : hops) {
        spec = spec.trimmed();
        if (spec.isEmpty())
            continue;
        JumpHost hop;
        hop.label = spec;
        QDomElement saved = findSiteElement(spec);
        if (!saved.isNull()) {
            const SiteInfo site = siteInfoFromElement(saved);
            hop.hostname = site.hostname;
            hop.port = site.port > 0 ? site.port : 22;
            hop.username = site.username;
            hop.password = site.password;
            hop.privateKeyPath = site.privateKeyPath;
            hop.certificatePath = site.certificatePath;
        } else {
            // [user@]host[:port], host may be [ipv6]
            QString rest = spec;
            const int at = rest.lastIndexOf('@');
            if (at >= 0) {
                hop.username = rest.left(at);
                rest = rest.mid(at + 1);
            }
            if (rest.startsWith('[')) {
                const int close = rest.indexOf(']');
                hop.hostname = rest.mid(1, close - 1);
                if (rest.mid(close + 1).startsWith(':'))
                    hop.port = rest.mid(close + 2).toInt();
            } else if (rest.count(':') == 1) {
                hop.hostname = rest.section(':', 0, 0);
                hop.port = rest.section(':', 1).toInt();
            } else {
                hop.hostname = rest;
            }
            if (hop.port <= 0)
                hop.port = 22;
        }
        if (!hop.hostname.isEmpty())
            info.jumpChain << hop;
    }
}

bool SiteTree::storePassword(const QString &siteName, const QString &password)
{
    QDomElement element = findSiteElement(siteName);
    if (element.isNull())
        return false;
    SiteInfo info = siteInfoFromElement(element);
    info.password = password;
    writeSiteElement(sharedDoc(), element, info);
    if (!saveShared())
        return false;
    for (SiteTree *tree : allTrees())
        tree->rebuild();
    return true;
}

// cut/copy buffer, shared by all trees
static QDomElement clipElement;
static bool clipIsCut = false;

static bool isFolderItem(const QTreeWidgetItem *item)
{
    return item && item->data(0, DomElementRole).value<TreeNode>().isFolder;
}

static QDomElement elementOf(const QTreeWidgetItem *item)
{
    return item ? item->data(0, DomNodeRole).value<QDomElement>() : QDomElement();
}

static bool isAncestorOrSelf(const QDomNode &ancestor, QDomNode node)
{
    for (; !node.isNull(); node = node.parentNode())
        if (node == ancestor)
            return true;
    return false;
}

// path of folder names, used to keep folders collapsed across rebuilds
static QString folderPath(const QTreeWidgetItem *item)
{
    QStringList parts;
    for (; item; item = item->parent())
        parts.prepend(item->text(0));
    return parts.join(QChar('/'));
}

// ---------------------------------------------------------------------------
// SiteTree

bool SiteTree::editSiteInfo(QWidget *parent, const QString &title, SiteInfo &info)
{
    return editSiteDialog(parent, title, info);
}

SiteTree::SiteTree(QWidget *parent)
    : QTreeWidget(parent)
{
    setHeaderHidden(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setUniformRowHeights(true);
    setAnimated(true);
    setIconSize(QSize(16, 16));

    auto make = [this](QIcon icon, const QString &text, const QKeySequence &key, auto slot) {
        QAction *a = new QAction(icon, text, this);
        if (!key.isEmpty()) {
            a->setShortcut(key);
            a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
            a->setToolTip(QStringLiteral("%1 (%2)").arg(text, key.toString(QKeySequence::NativeText)));
        }
        connect(a, &QAction::triggered, this, slot);
        addAction(a);   // makes the shortcut work while the tree has focus
        return a;
    };
    actConnect = make(qssh::themeIcon({"go-jump", "media-playback-start"}, QStyle::SP_MediaPlay),
                      tr("Connect"), QKeySequence(), &SiteTree::connectSelected);
    actConnect->setToolTip(tr("Connect to the selected session (Enter)"));
    actNewSession = make(qssh::themeIcon({"document-new"}, QStyle::SP_FileIcon),
                         tr("New Session..."), QKeySequence(), &SiteTree::newSession);
    actNewSession->setToolTip(tr("New session"));
    actNewFolder = make(qssh::themeIcon({"folder-new"}, QStyle::SP_FileDialogNewFolder),
                        tr("New Folder..."), QKeySequence(), &SiteTree::newFolder);
    actNewFolder->setToolTip(tr("New folder"));
    actCut = make(qssh::themeIcon({"edit-cut"}, QStyle::SP_DialogDiscardButton),
                  tr("Cut"), QKeySequence::Cut, &SiteTree::cut);
    actCopy = make(qssh::themeIcon({"edit-copy"}, QStyle::SP_FileIcon),
                   tr("Copy"), QKeySequence::Copy, &SiteTree::copy);
    actPaste = make(qssh::themeIcon({"edit-paste"}, QStyle::SP_DialogApplyButton),
                    tr("Paste"), QKeySequence::Paste, &SiteTree::paste);
    actDelete = make(qssh::themeIcon({"edit-delete"}, QStyle::SP_TrashIcon),
                     tr("Delete"), QKeySequence::Delete, &SiteTree::deleteSelected);
    actRename = make(QIcon(), tr("Rename"), QKeySequence(Qt::Key_F2), &SiteTree::renameSelected);
    actProperties = make(qssh::themeIcon({"document-properties"}, QStyle::SP_FileDialogDetailedView),
                         tr("Properties"), QKeySequence(Qt::ALT | Qt::Key_Return),
                         &SiteTree::properties);

    connect(this, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item) {
        if (item && !isFolderItem(item))
            connectSelected();
    });
    connect(this, &QTreeWidget::currentItemChanged, this, &SiteTree::updateActions);

    ensureLoaded(parent);
    allTrees().append(this);
    rebuild();
}

SiteTree::~SiteTree()
{
    allTrees().removeAll(this);
}

void SiteTree::rebuild()
{
    QSet<QString> collapsed;
    QDomElement selected = elementOf(currentItem());
    for (QTreeWidgetItemIterator it(this); *it; ++it)
        if (isFolderItem(*it) && !(*it)->isExpanded())
            collapsed.insert(folderPath(*it));

    clear();
    QDomElement root = sharedDoc().documentElement();
    rootItem = new QTreeWidgetItem(this);
    rootItem->setText(0, tr("Sessions"));
    rootItem->setIcon(0, qssh::themeIcon({"folder"}, QStyle::SP_DirIcon));
    rootItem->setData(0, DomElementRole, QVariant::fromValue(TreeNode()));
    rootItem->setData(0, DomNodeRole, QVariant::fromValue(root));
    parseNode(root, rootItem);

    for (QTreeWidgetItemIterator it(this); *it; ++it)
        if (isFolderItem(*it))
            (*it)->setExpanded(!collapsed.contains(folderPath(*it)));

    if (QTreeWidgetItem *item = itemForElement(selected))
        setCurrentItem(item);
    setFilter(filterText);
    updateActions();
}

void SiteTree::parseNode(const QDomElement &element, QTreeWidgetItem *parentItem)
{
    for (QDomElement child = element.firstChildElement(); !child.isNull();
         child = child.nextSiblingElement()) {
        if (child.tagName() == folderElement())
            parseNode(child, addFolderItem(child, parentItem));
        else if (child.tagName() == siteElement())
            addSiteItem(child, parentItem);
    }
}

QTreeWidgetItem *SiteTree::addFolderItem(const QDomElement &element, QTreeWidgetItem *parentItem)
{
    static const QIcon icon = qssh::themeIcon({"folder"}, QStyle::SP_DirIcon);
    QTreeWidgetItem *item = new QTreeWidgetItem(parentItem);
    QString title = element.attribute(nameAttribute());
    item->setText(0, title.isEmpty() ? tr("Folder") : title);
    item->setIcon(0, icon);
    item->setData(0, DomElementRole, QVariant::fromValue(TreeNode()));
    item->setData(0, DomNodeRole, QVariant::fromValue(element));
    return item;
}

QTreeWidgetItem *SiteTree::addSiteItem(const QDomElement &element, QTreeWidgetItem *parentItem)
{
    static const QIcon icon = qssh::themeIcon({"utilities-terminal", "computer", "network-server"},
                                              QStyle::SP_ComputerIcon);
    QTreeWidgetItem *item = new QTreeWidgetItem(parentItem);
    TreeNode node;
    node.isFolder = false;
    node.siteInfo = siteInfoFromElement(element);
    item->setText(0, node.siteInfo.sitename);
    item->setIcon(0, icon);
    const SiteInfo &s = node.siteInfo;
    item->setToolTip(0, QStringLiteral("%1%2:%3")
                            .arg(s.username.isEmpty() ? QString() : s.username + '@', s.hostname)
                            .arg(s.port > 0 ? s.port : 22));
    item->setData(0, DomElementRole, QVariant::fromValue(node));
    item->setData(0, DomNodeRole, QVariant::fromValue(element));
    return item;
}

QTreeWidgetItem *SiteTree::itemForElement(const QDomElement &element) const
{
    if (element.isNull())
        return nullptr;
    for (QTreeWidgetItemIterator it(const_cast<SiteTree *>(this)); *it; ++it)
        if (elementOf(*it) == element)
            return *it;
    return nullptr;
}

// Folder that new entries go into: the selected folder, or the folder
// containing the selected site, or the top level.
QTreeWidgetItem *SiteTree::targetFolderItem() const
{
    QTreeWidgetItem *cur = currentItem();
    if (!cur)
        return rootItem;
    return isFolderItem(cur) ? cur : cur->parent();
}

// Saves the shared document and refreshes every open tree
void SiteTree::commit(const QDomElement &select)
{
    if (!saveShared())
        QMessageBox::warning(window(), tr("Session Manager"),
                             tr("Cannot write %1").arg(sitesFilePath()));
    for (SiteTree *tree : allTrees())
        tree->rebuild();
    if (QTreeWidgetItem *item = itemForElement(select)) {
        for (QTreeWidgetItem *p = item->parent(); p; p = p->parent())
            p->setExpanded(true);
        setCurrentItem(item);
        scrollToItem(item);
    }
}

void SiteTree::updateActions()
{
    QTreeWidgetItem *cur = currentItem();
    const bool editable = cur && cur != rootItem;
    actConnect->setEnabled(hasSiteSelected());
    actCut->setEnabled(editable);
    actCopy->setEnabled(editable);
    actPaste->setEnabled(!clipElement.isNull() && !clipElement.parentNode().isNull());
    actDelete->setEnabled(editable);
    actRename->setEnabled(editable);
    actProperties->setEnabled(editable);
}

bool SiteTree::hasSiteSelected() const
{
    return currentItem() && !isFolderItem(currentItem());
}

SiteInfo SiteTree::selectedSite() const
{
    return hasSiteSelected() ? currentItem()->data(0, DomElementRole).value<TreeNode>().siteInfo
                             : SiteInfo();
}

bool SiteTree::findSite(const QString &name, SiteInfo *info) const
{
    for (QTreeWidgetItemIterator it(const_cast<SiteTree *>(this)); *it; ++it) {
        const TreeNode node = (*it)->data(0, DomElementRole).value<TreeNode>();
        if (!node.isFolder && node.siteInfo.sitename.compare(name, Qt::CaseInsensitive) == 0) {
            *info = node.siteInfo;
            return true;
        }
    }
    return false;
}

void SiteTree::connectSelected()
{
    if (hasSiteSelected())
        emit connectRequested(selectedSite());
}

void SiteTree::addSite(const SiteInfo &info)
{
    QDomElement siteEl = sharedDoc().createElement(siteElement());
    writeSiteElement(sharedDoc(), siteEl, info);
    elementOf(targetFolderItem()).appendChild(siteEl);
    commit(siteEl);
}

void SiteTree::newSession()
{
    SiteInfo info;
    if (editSiteDialog(window(), tr("New Session"), info))
        addSite(info);
}

void SiteTree::newFolder()
{
    bool ok = false;
    const QString name = QInputDialog::getText(window(), tr("New Folder"), tr("Folder name:"),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;
    QDomElement folderEl = sharedDoc().createElement(folderElement());
    folderEl.setAttribute(nameAttribute(), name);
    elementOf(targetFolderItem()).appendChild(folderEl);
    commit(folderEl);
}

void SiteTree::cut()
{
    QTreeWidgetItem *cur = currentItem();
    if (!cur || cur == rootItem)
        return;
    clipElement = elementOf(cur);
    clipIsCut = true;
    for (SiteTree *tree : allTrees())
        tree->updateActions();
}

void SiteTree::copy()
{
    QTreeWidgetItem *cur = currentItem();
    if (!cur || cur == rootItem)
        return;
    clipElement = elementOf(cur);
    clipIsCut = false;
    for (SiteTree *tree : allTrees())
        tree->updateActions();
}

void SiteTree::paste()
{
    if (clipElement.isNull() || clipElement.parentNode().isNull())
        return;
    QDomElement target = elementOf(targetFolderItem());
    if (clipIsCut && isAncestorOrSelf(clipElement, target)) {
        QMessageBox::information(window(), tr("Paste"), tr("A folder cannot be moved into itself."));
        return;
    }

    QDomElement pasted;
    if (clipIsCut) {
        pasted = clipElement;
        target.appendChild(pasted);   // appendChild moves the node
        clipElement = QDomElement();
    } else {
        pasted = clipElement.cloneNode(true).toElement();
        // avoid two entries with the same name in one folder
        const QString name = pasted.attribute(nameAttribute());
        for (QDomElement sib = target.firstChildElement(); !sib.isNull(); sib = sib.nextSiblingElement()) {
            if (sib.attribute(nameAttribute()) == name) {
                pasted.setAttribute(nameAttribute(), tr("%1 - Copy").arg(name));
                break;
            }
        }
        target.appendChild(pasted);
    }
    commit(pasted);
}

void SiteTree::deleteSelected()
{
    QTreeWidgetItem *item = currentItem();
    if (!item || item == rootItem)
        return;
    const QString question = isFolderItem(item)
        ? tr("Delete folder \"%1\" and all sessions in it?").arg(item->text(0))
        : tr("Delete session \"%1\"?").arg(item->text(0));
    if (QMessageBox::question(window(), tr("Delete"), question) != QMessageBox::Yes)
        return;
    QDomElement element = elementOf(item);
    if (isAncestorOrSelf(element, clipElement))
        clipElement = QDomElement();
    element.parentNode().removeChild(element);
    commit();
}

void SiteTree::renameSelected()
{
    QTreeWidgetItem *item = currentItem();
    if (!item || item == rootItem)
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(window(), tr("Rename"), tr("New name:"),
                                               QLineEdit::Normal, item->text(0), &ok).trimmed();
    if (!ok || name.isEmpty() || name == item->text(0))
        return;
    QDomElement element = elementOf(item);
    element.setAttribute(nameAttribute(), name);
    commit(element);
}

void SiteTree::properties()
{
    QTreeWidgetItem *item = currentItem();
    if (!item || item == rootItem)
        return;
    if (isFolderItem(item)) {
        renameSelected();
        return;
    }
    QDomElement element = elementOf(item);
    SiteInfo info = siteInfoFromElement(element);
    if (!editSiteDialog(window(), tr("Session Properties"), info))
        return;
    writeSiteElement(sharedDoc(), element, info);
    commit(element);
}

void SiteTree::setFilter(const QString &text)
{
    filterText = text.trimmed();
    if (!rootItem)
        return;
    applyFilter(rootItem, filterText.isEmpty());
    rootItem->setHidden(false);
    if (!filterText.isEmpty())
        expandAll();
}

// An item is shown when it matches, an enclosing folder matches, or
// something inside it matches.
bool SiteTree::applyFilter(QTreeWidgetItem *item, bool ancestorMatches)
{
    const bool self = item != rootItem && item->text(0).contains(filterText, Qt::CaseInsensitive);
    bool anyChild = false;
    for (int i = 0; i < item->childCount(); ++i)
        anyChild |= applyFilter(item->child(i), ancestorMatches || self);
    const bool visible = ancestorMatches || self || anyChild;
    item->setHidden(!visible);
    return visible;
}

void SiteTree::contextMenuEvent(QContextMenuEvent *event)
{
    QTreeWidgetItem *item = itemAt(event->pos());
    if (item)
        setCurrentItem(item);
    updateActions();

    QMenu menu;
    if (item && !isFolderItem(item)) {
        menu.addAction(actConnect);
        QFont bold = menu.font();
        bold.setBold(true);
        actConnect->setFont(bold);
        menu.addSeparator();
    }
    menu.addAction(actCut);
    menu.addAction(actCopy);
    menu.addAction(actPaste);
    menu.addSeparator();
    menu.addAction(actDelete);
    menu.addAction(actRename);
    menu.addSeparator();
    menu.addAction(actNewSession);
    menu.addAction(actNewFolder);
    menu.addSeparator();
    menu.addAction(actProperties);
    menu.exec(event->globalPos());
    actConnect->setFont(QFont());
}

// ---------------------------------------------------------------------------
// QSessionManagerWidget

QSessionManagerWidget::QSessionManagerWidget(QWidget *parent)
    : QWidget(parent)
{
    siteTree = new SiteTree(this);
    connect(siteTree, &SiteTree::connectRequested, this, &QSessionManagerWidget::connectRequested);

    QToolBar *bar = new QToolBar(this);
    bar->setIconSize(QSize(16, 16));
    bar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    bar->addAction(siteTree->actConnect);
    bar->addSeparator();
    bar->addAction(siteTree->actNewSession);
    bar->addAction(siteTree->actNewFolder);
    bar->addSeparator();
    bar->addAction(siteTree->actCut);
    bar->addAction(siteTree->actCopy);
    bar->addAction(siteTree->actPaste);
    bar->addSeparator();
    bar->addAction(siteTree->actDelete);
    bar->addAction(siteTree->actProperties);

    filter = new QLineEdit(this);
    filter->setPlaceholderText(tr("Filter by folder/session name <Alt+I>"));
    filter->setClearButtonEnabled(true);
    filter->addAction(qssh::themeIcon({"edit-find"}, QStyle::SP_FileDialogContentsView),
                      QLineEdit::TrailingPosition);
    connect(filter, &QLineEdit::textChanged, siteTree, &SiteTree::setFilter);
    // Enter in the filter connects to the first visible session
    connect(filter, &QLineEdit::returnPressed, this, [this]() {
        for (QTreeWidgetItemIterator it(siteTree, QTreeWidgetItemIterator::NotHidden); *it; ++it) {
            if (!isFolderItem(*it)) {
                siteTree->setCurrentItem(*it);
                siteTree->connectSelected();
                return;
            }
        }
    });
    QShortcut *filterKey = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_I), this);
    filterKey->setContext(Qt::WidgetWithChildrenShortcut);
    connect(filterKey, &QShortcut::activated, this, &QSessionManagerWidget::focusFilter);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(bar);
    layout->addWidget(filter);
    layout->addWidget(siteTree);
}

void QSessionManagerWidget::focusFilter()
{
    filter->setFocus();
    filter->selectAll();
}

// ---------------------------------------------------------------------------
// QSiteTreeDialog

QSiteTreeDialog::QSiteTreeDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Connect"));
    manager = new QSessionManagerWidget(this);
    connect(manager, &QSessionManagerWidget::connectRequested, this, [this](SiteInfo info) {
        emit notifyNewSession(info);
        accept();
    });

    QSettings settings(QDir::homePath() + "/.qsshterm/qsshterm.ini", QSettings::IniFormat);
    showOnStartup = new QCheckBox(tr("Show dialog on startup"), this);
    showOnStartup->setChecked(settings.value("connectDialog/showOnStartup", true).toBool());
    connect(showOnStartup, &QCheckBox::toggled, this, [](bool on) {
        QSettings s(QDir::homePath() + "/.qsshterm/qsshterm.ini", QSettings::IniFormat);
        s.setValue("connectDialog/showOnStartup", on);
    });

    QPushButton *connectButton = new QPushButton(tr("&Connect"), this);
    connectButton->setDefault(true);
    connectButton->setEnabled(tree()->hasSiteSelected());
    connect(connectButton, &QPushButton::clicked, tree(), &SiteTree::connectSelected);
    connect(tree(), &QTreeWidget::currentItemChanged, this, [this, connectButton]() {
        connectButton->setEnabled(tree()->hasSiteSelected());
    });
    QPushButton *closeButton = new QPushButton(tr("Close"), this);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::reject);

    QHBoxLayout *bottom = new QHBoxLayout;
    bottom->addWidget(showOnStartup);
    bottom->addStretch(1);
    bottom->addWidget(connectButton);
    bottom->addWidget(closeButton);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(manager);
    layout->addLayout(bottom);
    resize(420, 520);
}

void QSiteTreeDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    manager->focusFilter();
}

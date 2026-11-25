#include <QtWidgets>

#include "qsiteconfig.h"

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
        } else if (tagName == "password") {
            info.password = elm.text();
        }
    }
    return info;
}

static void writeSiteElement(QDomDocument &doc, QDomElement &element, const SiteInfo &info)
{
    element.setAttribute(nameAttribute(), info.sitename);
    // drop the old connection settings, keep anything else untouched
    const QStringList managed = { hostElement(), userElement(), privateElement(), "password" };
    for (QDomElement elm = element.firstChildElement(); !elm.isNull();) {
        QDomElement next = elm.nextSiblingElement();
        if (managed.contains(elm.tagName()))
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
    if (!info.privateKeyPath.isEmpty()) {
        QDomElement key = doc.createElement(privateElement());
        key.setAttribute(fileAttribute(), info.privateKeyPath);
        element.appendChild(key);
    }
}

// Modal editor for one site's connection settings
static bool editSiteInfo(QWidget *parent, const QString &title, SiteInfo &info)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(title);

    QLineEdit *name = new QLineEdit(info.sitename, &dlg);
    QLineEdit *host = new QLineEdit(info.hostname, &dlg);
    QSpinBox *port = new QSpinBox(&dlg);
    port->setRange(1, 65535);
    port->setValue(info.port > 0 ? info.port : 22);
    QLineEdit *user = new QLineEdit(info.username, &dlg);
    QLineEdit *pass = new QLineEdit(info.password, &dlg);
    pass->setEchoMode(QLineEdit::Password);
    QLineEdit *key = new QLineEdit(info.privateKeyPath, &dlg);
    QPushButton *browse = new QPushButton(QObject::tr("Browse..."), &dlg);
    QHBoxLayout *keyLayout = new QHBoxLayout;
    keyLayout->addWidget(key);
    keyLayout->addWidget(browse);
    QObject::connect(browse, &QPushButton::clicked, &dlg, [&dlg, key]() {
        const QString file = QFileDialog::getOpenFileName(&dlg, QObject::tr("Select private key"), key->text());
        if (!file.isEmpty())
            key->setText(file);
    });

    QFormLayout *form = new QFormLayout;
    form->addRow(QObject::tr("Name:"), name);
    form->addRow(QObject::tr("Host:"), host);
    form->addRow(QObject::tr("Port:"), port);
    form->addRow(QObject::tr("Username:"), user);
    form->addRow(QObject::tr("Password:"), pass);
    form->addRow(QObject::tr("Private key:"), keyLayout);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, [&dlg, host]() {
        if (host->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dlg, dlg.windowTitle(), QObject::tr("Host must not be empty."));
            return;
        }
        dlg.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    QVBoxLayout *layout = new QVBoxLayout(&dlg);
    layout->addLayout(form);
    layout->addWidget(buttons);
    dlg.resize(400, dlg.sizeHint().height());

    if (dlg.exec() != QDialog::Accepted)
        return false;

    info.hostname = host->text().trimmed();
    info.sitename = name->text().trimmed().isEmpty() ? info.hostname : name->text().trimmed();
    info.port = port->value();
    info.username = user->text().trimmed();
    info.password = pass->text();
    info.privateKeyPath = key->text().trimmed();
    return true;
}

SiteTree::SiteTree(QWidget *parent)
    : QTreeWidget(parent)
{
    // QStringList labels;
    // labels << tr("Title") << tr("Location");

    // header()->setSectionResizeMode(QHeaderView::Stretch);
    // setHeaderLabels(labels);

    // folderIcon.addPixmap(style()->standardPixmap(QStyle::SP_DirClosedIcon),
    //                      QIcon::Normal, QIcon::Off);
    // folderIcon.addPixmap(style()->standardPixmap(QStyle::SP_DirOpenIcon),
    //                      QIcon::Normal, QIcon::On);
    // bookmarkIcon.addPixmap(style()->standardPixmap(QStyle::SP_FileIcon));
    setHeaderHidden(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    folderIcon.addFile("./icon/folder.icon");

    xmlfile.setFileName(sitesFilePath());
    connect(this,SIGNAL(itemDoubleClicked(QTreeWidgetItem *, int)),this,SLOT(Ondoubleclicktree(QTreeWidgetItem *, int)));
    load();
}

void SiteTree::Ondoubleclicktree(QTreeWidgetItem *item, int column)
{
    Q_UNUSED(column);
    if (!item)
        return;
    QVariant data = item->data(0, DomElementRole);
    TreeNode node = data.value<TreeNode>();
    if (node.isFolder) {
        return;
    }
    QSiteTreeDialog * dialog = qobject_cast<QSiteTreeDialog*>(window());
    if (dialog)
        emit dialog->notifyNewSession(node.siteInfo);
}

#if !defined(QT_NO_CONTEXTMENU) && !defined(QT_NO_CLIPBOARD)
void SiteTree::contextMenuEvent(QContextMenuEvent *event)
{

    QTreeWidgetItem *item = itemAt(event->pos());
    setCurrentItem(item);
    if (!item)
        clearSelection();
    QMenu contextMenu;
    if (item) {
        TreeNode node = item->data(0, DomElementRole).value<TreeNode>();

        if (!node.isFolder) {
            connect(contextMenu.addAction(tr("Open")), &QAction::triggered, this, [this, item]() {
                this->Ondoubleclicktree(item, 0);
            });
            contextMenu.addSeparator();
            connect(contextMenu.addAction(tr("New Site")), &QAction::triggered, this, &SiteTree::newSession);
            connect(contextMenu.addAction(tr("Edit")), &QAction::triggered, this, &SiteTree::editSession);
            connect(contextMenu.addAction(tr("Delete")), &QAction::triggered, this, &SiteTree::deleteSession);
        } else {
            connect(contextMenu.addAction(tr("New Site")), &QAction::triggered, this, &SiteTree::newSession);
            connect(contextMenu.addAction(tr("New Folder")), &QAction::triggered, this, &SiteTree::newFolder);
            contextMenu.addSeparator();
            connect(contextMenu.addAction(tr("Rename")), &QAction::triggered, this, &SiteTree::editSession);
            connect(contextMenu.addAction(tr("Delete")), &QAction::triggered, this, &SiteTree::deleteSession);
        }
    } else {
        connect(contextMenu.addAction(tr("New Site")), &QAction::triggered, this, &SiteTree::newSession);
        connect(contextMenu.addAction(tr("New Folder")), &QAction::triggered, this, &SiteTree::newFolder);
    }
    contextMenu.exec(event->globalPos());
}
#endif // !QT_NO_CONTEXTMENU && !QT_NO_CLIPBOARD

// Sites live in ~/.qsshterm/sites.xml. On first run, seed it from the
// legacy ./config/sites.xml if present, otherwise from an empty <sites/>.
QString SiteTree::sitesFilePath()
{
    const QString dirPath = QDir::homePath() + "/.qsshterm";
    const QString path = dirPath + "/sites.xml";
    if (QFile::exists(path))
        return path;

    QDir().mkpath(dirPath);
    const QString legacy = "./config/sites.xml";
    if (QFile::exists(legacy) && QFile::copy(legacy, path)) {
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        return path;
    }

    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        f.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<sites>\n</sites>\n");
        f.close();
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }
    return path;
}

bool SiteTree::load()
{
    if (!xmlfile.open(QIODevice::ReadOnly)) {
        QMessageBox::information(window(), tr("DOM Bookmarks"),
                                 tr("Cannot read %1:\n%2")
                                 .arg(xmlfile.fileName(), xmlfile.errorString()));
        return false;
    }
    const QDomDocument::ParseResult result =
        domDocument.setContent(&xmlfile, QDomDocument::ParseOption::UseNamespaceProcessing);
    xmlfile.close();
    if (!result) {
        QMessageBox::information(window(), tr("DOM Bookmarks"),
                                 tr("Parse error at line %1, column %2:\n%3")
                                 .arg(result.errorLine)
                                 .arg(result.errorColumn)
                                 .arg(result.errorMessage));
        return false;
    }

    QDomElement root = domDocument.documentElement();

    if (root.tagName() != "sites") {
        QMessageBox::information(window(), tr("DOM Bookmarks"),
                                 tr("The file is not an session config file."));
        return false;
    } 

    clear();

    disconnect(this, &QTreeWidget::itemChanged, this, &SiteTree::updateDomElement);

    parseNode(root);
    connect(this, &QTreeWidget::itemChanged, this, &SiteTree::updateDomElement);

    return true;
}

void SiteTree::parseNode(const QDomNode &element, QTreeWidgetItem *parentItem)
{
      QDomNodeList list = element.childNodes();
      for (int j = 0; j < list.size(); j++) {
            QDomNode node = list.at(j);
            if (node.nodeName() == "folder") {
                QTreeWidgetItem * pnode = parseFolderElement(node.toElement(), parentItem);
                parseNode(node, pnode);
            } else if (node.nodeName() == "site")
                parseSite(node, parentItem);
      }
}

static bool isFolderItem(const QTreeWidgetItem *item)
{
    return item && item->data(0, DomElementRole).value<TreeNode>().isFolder;
}

static QDomElement elementOf(const QTreeWidgetItem *item)
{
    return item ? item->data(0, DomNodeRole).value<QDomElement>() : QDomElement();
}

// Folder that new entries go into: the selected folder, or the folder
// containing the selected site, or the top level.
QTreeWidgetItem *SiteTree::targetFolderItem() const
{
    QTreeWidgetItem *cur = currentItem();
    if (!cur || !cur->isSelected())
        return nullptr;
    return isFolderItem(cur) ? cur : cur->parent();
}

QDomElement SiteTree::folderElementFor(QTreeWidgetItem *folderItem)
{
    QDomElement parent = elementOf(folderItem);
    if (parent.isNull()) {
        parent = domDocument.documentElement();
        if (parent.isNull()) {
            parent = domDocument.createElement("sites");
            domDocument.appendChild(parent);
        }
    }
    return parent;
}

void SiteTree::saveOrWarn()
{
    if (!save())
        QMessageBox::warning(window(), tr("Session Manager"),
                             tr("Cannot write %1").arg(xmlfile.fileName()));
}

void SiteTree::newSession()
{
    SiteInfo info;
    if (!editSiteInfo(window(), tr("New Session"), info))
        return;

    QTreeWidgetItem *parentItem = targetFolderItem();
    QDomElement parentEl = folderElementFor(parentItem);
    QDomElement siteEl = domDocument.createElement(siteElement());
    writeSiteElement(domDocument, siteEl, info);
    parentEl.appendChild(siteEl);

    parseSite(siteEl, parentItem);
    QTreeWidgetItem *item = parentItem ? parentItem->child(parentItem->childCount() - 1)
                                       : topLevelItem(topLevelItemCount() - 1);
    if (parentItem)
        parentItem->setExpanded(true);
    setCurrentItem(item);
    saveOrWarn();
}

void SiteTree::newFolder()
{
    bool ok = false;
    const QString name = QInputDialog::getText(window(), tr("New Folder"), tr("Folder name:"),
                                               QLineEdit::Normal, QString(), &ok).trimmed();
    if (!ok || name.isEmpty())
        return;

    QTreeWidgetItem *parentItem = targetFolderItem();
    QDomElement parentEl = folderElementFor(parentItem);
    QDomElement folderEl = domDocument.createElement(folderElement());
    folderEl.setAttribute(nameAttribute(), name);
    parentEl.appendChild(folderEl);

    QTreeWidgetItem *item = parseFolderElement(folderEl, parentItem);
    if (parentItem)
        parentItem->setExpanded(true);
    setCurrentItem(item);
    saveOrWarn();
}

void SiteTree::editSession()
{
    QTreeWidgetItem *item = currentItem();
    QDomElement element = elementOf(item);
    if (!item || !item->isSelected() || element.isNull())
        return;

    if (isFolderItem(item)) {
        bool ok = false;
        const QString name = QInputDialog::getText(window(), tr("Rename Folder"), tr("Folder name:"),
                                                   QLineEdit::Normal, item->text(0), &ok).trimmed();
        if (!ok || name.isEmpty())
            return;
        element.setAttribute(nameAttribute(), name);
        item->setText(0, name);
    } else {
        SiteInfo info = siteInfoFromElement(element);
        if (!editSiteInfo(window(), tr("Edit Session"), info))
            return;
        writeSiteElement(domDocument, element, info);
        TreeNode node;
        node.isFolder = false;
        node.siteInfo = siteInfoFromElement(element);
        item->setData(0, DomElementRole, QVariant::fromValue(node));
        item->setText(0, info.sitename);
    }
    saveOrWarn();
}

void SiteTree::deleteSession()
{
    QTreeWidgetItem *item = currentItem();
    QDomElement element = elementOf(item);
    if (!item || !item->isSelected() || element.isNull())
        return;

    const QString question = isFolderItem(item)
        ? tr("Delete folder \"%1\" and all sessions in it?").arg(item->text(0))
        : tr("Delete session \"%1\"?").arg(item->text(0));
    if (QMessageBox::question(window(), tr("Delete"), question) != QMessageBox::Yes)
        return;

    element.parentNode().removeChild(element);
    delete item;
    saveOrWarn();
}

void SiteTree::parseSite(const QDomNode &node,
                                  QTreeWidgetItem *parentItem){
    QIcon machineico("./icon/site.ico");
    QDomElement element = node.toElement();
    QTreeWidgetItem *item = createItem(element, parentItem);

    QString title = element.attribute("name");

    item->setFlags(item->flags() | Qt::ItemIsEditable);
    item->setIcon(0, machineico);
    item->setText(0, title);
    
    SiteInfo info = siteInfoFromElement(element);
    TreeNode treenode;
    treenode.isFolder = false;
    treenode.siteInfo = info;
    item->setData(0, DomElementRole, QVariant::fromValue(treenode));
}

bool SiteTree::save() const
{
    const int IndentSize = 4;

    // previously wrote at the read position of a ReadWrite handle, appending
    // a second document to the file instead of replacing it
    QFile out(xmlfile.fileName());
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    QTextStream stream(&out);
    domDocument.save(stream, IndentSize);
    return true;
}

void SiteTree::updateDomElement(const QTreeWidgetItem *item, int column)
{
    QDomElement element = item->data(0, DomElementRole).value<QDomElement>();
    if (!element.isNull()) {
        if (column == 0) {
            QDomElement oldTitleElement = element.firstChildElement(siteElement());
            QDomElement newTitleElement = domDocument.createElement(siteElement());

            QDomText newTitleText = domDocument.createTextNode(item->text(0));
            newTitleElement.appendChild(newTitleText);

            element.replaceChild(newTitleElement, oldTitleElement);
        } else {
            if (element.tagName() == siteElement())
                element.setAttribute(nameAttribute(), item->text(1));
        }
    }
}

QTreeWidgetItem * SiteTree::parseFolderElement(const QDomElement &element,
                                  QTreeWidgetItem *parentItem)
{
    QIcon folderico("./icon/folder.ico");
    QTreeWidgetItem *item = createItem(element, parentItem);
    TreeNode node;
    item->setData(0, DomElementRole, QVariant::fromValue(node));
    QString title = element.attribute("name");
    if (title.isEmpty())
        title = QObject::tr("Folder");

    item->setFlags(item->flags() | Qt::ItemIsEditable);
    item->setIcon(0, folderico);
    item->setText(0, title);

    item->setExpanded(true);
    return item;
}

QTreeWidgetItem *SiteTree::createItem(const QDomElement &element,
                                      QTreeWidgetItem *parentItem)
{
    QTreeWidgetItem *item;
    if (parentItem) {
        item = new QTreeWidgetItem(parentItem);
    } else {
        item = new QTreeWidgetItem(this);
    }
    item->setData(0, DomNodeRole, QVariant::fromValue(element));
    return item;
}

QSiteTreeDialog::QSiteTreeDialog(QWidget *parent)
    : QDialog(parent)
{
   tree = new SiteTree(this);
   QVBoxLayout *mainLayout = new QVBoxLayout(this);

   toolbar = new QToolBar(this);
   mainLayout->addWidget(toolbar);
   mainLayout->addWidget(tree);

   this->setWindowTitle(QString::fromUtf8("SSH Session Manager"));
   //QGridLayout *gridLayout = new QGridLayout(this);

   //gridLayout->addWidget(tree,0,0,1,1);
   initToolbar();
   resize(400, 300);
}

void QSiteTreeDialog::initToolbar() {
    
    QIcon newicon("./icon/new.ico"); 
    QIcon editicon("./icon/edit.ico"); 
    QIcon deleteicon("./icon/delete.ico"); 
    QIcon newfoldericon("./icon/new-folder.ico"); 
    QAction * newAct = toolbar->addAction(newicon, "new");
    QAction * editAct = toolbar->addAction(editicon, "edit");
    QAction * deleteAct = toolbar->addAction(deleteicon, "delete");
    QAction * newFolderAct = toolbar->addAction(newfoldericon, "new folder");
    connect(newAct, &QAction::triggered, tree, &SiteTree::newSession);
    connect(editAct, &QAction::triggered, tree, &SiteTree::editSession);
    connect(deleteAct, &QAction::triggered, tree, &SiteTree::deleteSession);
    connect(newFolderAct, &QAction::triggered, tree, &SiteTree::newFolder);
}
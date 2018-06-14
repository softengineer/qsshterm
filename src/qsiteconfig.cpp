#include <QtWidgets>

#include "qsiteconfig.h"

enum { DomElementRole = Qt::UserRole + 1 };

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

    xmlfile.setFileName("./config/sites.xml");  
    xmlfile.open(QIODevice::ReadOnly | QIODevice::WriteOnly);  
    connect(this,SIGNAL(itemDoubleClicked(QTreeWidgetItem *, int)),this,SLOT(Ondoubleclicktree(QTreeWidgetItem *, int)));
    load();
}

void SiteTree::Ondoubleclicktree(QTreeWidgetItem *item, int column)
{
    QVariant data = item->data(0, DomElementRole);
    TreeNode node = data.value<TreeNode>();
    if (node.isFolder) {
        return;
    }
    QSiteTreeDialog * dialog = static_cast<QSiteTreeDialog*>(parent());
    emit dialog->notifyNewSession(node.siteInfo);
}

#if !defined(QT_NO_CONTEXTMENU) && !defined(QT_NO_CLIPBOARD)
void SiteTree::contextMenuEvent(QContextMenuEvent *event)
{

    QTreeWidgetItem *item = itemAt(event->pos());
    QMenu contextMenu;
    if (item) {
        QVariant data = item->data(0, DomElementRole);
        TreeNode node = data.value<TreeNode>();
        if (!item)
            return;
        const QString url = item->text(1);
       
        
        if (!node.isFolder) {
            QAction *openAction = contextMenu.addAction(tr("Open"));
            QAction *renameAction = contextMenu.addAction(tr("Rename"));
            QAction *newSiteAction = contextMenu.addAction(tr("New Site"));
            QAction *editAction = contextMenu.addAction(tr("Edit"));
            QAction *deleteAction = contextMenu.addAction(tr("Delete"));
            contextMenu.addSeparator();
            QAction *copyAction = contextMenu.addAction(tr("Copy"));
            QAction *pasterAction = contextMenu.addAction(tr("Paste"));
            QAction *cutAction = contextMenu.addAction(tr("Cut"));

            connect(openAction, &QAction::triggered, [this, item]() {
                this->Ondoubleclicktree(item, 0);
            });
        } else {
            QAction *newSiteAction = contextMenu.addAction(tr("New Site"));
            contextMenu.addSeparator();
            QAction *renameAction = contextMenu.addAction(tr("Rename"));
            QAction *deleteAction = contextMenu.addAction(tr("Delete"));
        }
    } else {
            QAction *newSiteAction = contextMenu.addAction(tr("New Site"));
            QAction *newFolderAction = contextMenu.addAction(tr("New Folder"));
    }
    QAction *action = contextMenu.exec(event->globalPos());
   
}
#endif // !QT_NO_CONTEXTMENU && !QT_NO_CLIPBOARD

bool SiteTree::load()
{
    QString errorStr;
    int errorLine;
    int errorColumn;

    if (!domDocument.setContent(const_cast<QFile *>(&xmlfile), true, &errorStr, &errorLine,
                                &errorColumn)) {
        QMessageBox::information(window(), tr("DOM Bookmarks"),
                                 tr("Parse error at line %1, column %2:\n%3")
                                 .arg(errorLine)
                                 .arg(errorColumn)
                                 .arg(errorStr));
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
      for (int j = 0; j< list.length(); j++) {
            QDomNode node = list.at(j);
            if (node.nodeName() == "folder") {
                QTreeWidgetItem * pnode = parseFolderElement(node.toElement(), parentItem);
                parseNode(node, pnode);
            } else if (node.nodeName() == "site")
                parseSite(node, parentItem);
      }
}

void SiteTree::newSession(){}
void SiteTree::newFolder(){}
void SiteTree::editSession(){}
void SiteTree::deleteSession(){}

void SiteTree::parseSite(const QDomNode &node,
                                  QTreeWidgetItem *parentItem){
    QIcon machineico("./icon/site.ico");
    QDomElement element = node.toElement();
    QTreeWidgetItem *item = createItem(element, parentItem);

    QString title = element.attribute("name");

    item->setFlags(item->flags() | Qt::ItemIsEditable);
    item->setIcon(0, machineico);
    item->setText(0, title);
    
    QDomNodeList children = node.childNodes();

    SiteInfo info;
    info.sitename = title;
    
    for (int j=0; j<children.length(); j++) {
        QDomNode child = children.at(j);
        QDomElement elm = child.toElement();
        QString tagName = elm.tagName();
        if  (tagName== "host") {
                info.hostname = elm.attribute("address");
                bool ok;
                info.port = elm.attribute("port").toInt(&ok, 10);
        } else if (tagName== "username") {
                info.username = elm.text();
        } else if (tagName== "privatekey") {
                info.privateKeyPath = elm.attribute("file");
        } else if (tagName == "password") {
                info.password = elm.text();
        }
    }
    TreeNode treenode;
    treenode.isFolder = false;
    treenode.siteInfo = info;
    item->setData(0, DomElementRole, QVariant::fromValue(treenode));
}

bool SiteTree::save() const
{
    const int IndentSize = 4;

    QTextStream out(const_cast<QFile *>(&xmlfile));
    domDocument.save(out, IndentSize);
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

    bool folded = true;
    setItemExpanded(item, folded);
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
    return item;
}

QSiteTreeDialog::QSiteTreeDialog(QWidget *parent) 
{
   tree = new SiteTree(this);
   QVBoxLayout *mainLayout = new QVBoxLayout(this);

   toolbar = new QToolBar();
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
    //connect(mgr, &QAction::triggered, this, &qsshTabTerm::showSessionMgrDialog);
}
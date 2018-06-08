#ifndef SITETREE_H
#define SITETREE_H

#include <QDomDocument>
#include <QIcon>
#include <QTreeWidget>
#include <QDialog>
#include <QFile>
#include <QVariant>

struct SiteInfo {
    QString sitename;
    QString hostname;
    QString username;
    QString password;
    QString privateKeyPath;

    int port;
    bool transferX11;

    SiteInfo() : sitename(QString()), 
    hostname(QString()), 
    username(QString()), 
    password(QString()), 
    privateKeyPath(QString()), 
    port(-1), 
    transferX11(false) {
        
    }
};

Q_DECLARE_METATYPE(SiteInfo);

class SiteTree : public QTreeWidget
{
    Q_OBJECT

public:
    SiteTree(QWidget *parent = 0);

    bool read(QFile  & file);
    bool write(QFile  & file) const;

protected:
#if !defined(QT_NO_CONTEXTMENU) && !defined(QT_NO_CLIPBOARD)
    void contextMenuEvent(QContextMenuEvent *event) override;
#endif

private slots:
    void updateDomElement(const QTreeWidgetItem *item, int column);
    void Ondoubleclicktree(QTreeWidgetItem *item, int column);

private:
    QTreeWidgetItem * parseFolderElement(const QDomElement &element,
                            QTreeWidgetItem *parentItem = 0);
    void parseNode(const QDomNode &node, 
             QTreeWidgetItem *parentItem = 0);
    void parseSite(const QDomNode &element,
                                  QTreeWidgetItem *parentItem = 0);
    QTreeWidgetItem *createItem(const QDomElement &element,
                                QTreeWidgetItem *parentItem = 0);

    QDomDocument domDocument;
    QIcon folderIcon;
    QIcon bookmarkIcon;
};

class QSiteTreeDialog : public QDialog
{
    Q_OBJECT

public:
    QSiteTreeDialog(QWidget *parent = 0);
    ~QSiteTreeDialog(){};

    signals:
    void notifyNewSession(SiteInfo);

private :
    SiteTree * tree;
};

#endif
#ifndef SITETREE_H
#define SITETREE_H

#include <QMainWindow>
#include <QDomDocument>
#include <QIcon>
#include <QTreeWidget>
#include <QDialog>
#include <QFile>
#include <QVariant>
#include "qsshterm_common.h"

class SiteTree : public QTreeWidget
{
    Q_OBJECT

public:
    SiteTree(QWidget *parent = 0);

    bool load();
    bool save() const;

protected:
#if !defined(QT_NO_CONTEXTMENU) && !defined(QT_NO_CLIPBOARD)
    void contextMenuEvent(QContextMenuEvent *event) override;
#endif

public slots:
    void newSession();
    void newFolder();
    void editSession();
    void deleteSession();

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

    QFile xmlfile;
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
    void initToolbar();
    QToolBar * toolbar;
    SiteTree * tree;
};

#endif
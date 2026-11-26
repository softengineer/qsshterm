#ifndef SITETREE_H
#define SITETREE_H

#include <QDomDocument>
#include <QIcon>
#include <QTreeWidget>
#include <QDialog>
#include <QVariant>
#include "qsshterm_common.h"

class QAction;
class QLineEdit;
class QCheckBox;

/**
 * Tree of saved sessions (~/.qsshterm/sites.xml). All trees share one
 * document; any change is saved and every open tree is rebuilt, so the
 * docked manager and the Connect dialog always agree.
 */
class SiteTree : public QTreeWidget
{
    Q_OBJECT

public:
    explicit SiteTree(QWidget *parent = nullptr);
    ~SiteTree() override;

    static QString sitesFilePath();
    /** Modal editor for one site's connection settings. */
    static bool editSiteInfo(QWidget *parent, const QString &title, SiteInfo &info);

    /** Adds a site to the selected folder (or the top level) and saves. */
    void addSite(const SiteInfo &info);
    bool hasSiteSelected() const;
    SiteInfo selectedSite() const;
    /** Saved session named @p name (case insensitive), if any. */
    bool findSite(const QString &name, SiteInfo *info) const;

    // shared by the context menu, the manager toolbar and the shortcuts
    QAction *actConnect;
    QAction *actNewSession;
    QAction *actNewFolder;
    QAction *actCut;
    QAction *actCopy;
    QAction *actPaste;
    QAction *actDelete;
    QAction *actRename;
    QAction *actProperties;

signals:
    void connectRequested(SiteInfo info);

public slots:
    void connectSelected();
    void newSession();
    void newFolder();
    void cut();
    void copy();
    void paste();
    void deleteSelected();
    void renameSelected();
    void properties();
    void setFilter(const QString &text);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void rebuild();
    void updateActions();
    bool applyFilter(QTreeWidgetItem *item, bool ancestorMatches);
    void parseNode(const QDomElement &element, QTreeWidgetItem *parentItem);
    QTreeWidgetItem *addFolderItem(const QDomElement &element, QTreeWidgetItem *parentItem);
    QTreeWidgetItem *addSiteItem(const QDomElement &element, QTreeWidgetItem *parentItem);
    QTreeWidgetItem *targetFolderItem() const;
    QTreeWidgetItem *itemForElement(const QDomElement &element) const;
    void commit(const QDomElement &select = QDomElement());

    QTreeWidgetItem *rootItem = nullptr;
    QString filterText;
};

/**
 * Session manager panel: toolbar, filter box and the session tree.
 * Used both in the main window's dock and in the Connect dialog.
 */
class QSessionManagerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit QSessionManagerWidget(QWidget *parent = nullptr);
    SiteTree *tree() const { return siteTree; }
    void focusFilter();

signals:
    void connectRequested(SiteInfo info);

private:
    SiteTree *siteTree;
    QLineEdit *filter;
};

/** SecureCRT style "Connect" dialog. */
class QSiteTreeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit QSiteTreeDialog(QWidget *parent = nullptr);
    SiteTree *tree() const { return manager->tree(); }

signals:
    void notifyNewSession(SiteInfo);

protected:
    void showEvent(QShowEvent *event) override;

private:
    QSessionManagerWidget *manager;
    QCheckBox *showOnStartup;
};

#endif

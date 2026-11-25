#include "qsshterm.h"
#include "qsiteconfig.h"
#include "qsshterm_sftp.h"
#include "qsshterm_common.h"
#include <QtWidgets>

/**
 * Main window, laid out like SecureCRT: menu bar, toolbar with a quick
 * connect box, a dockable Session Manager on the left, session tabs in the
 * middle and a status bar with connection details.
 */
class qsshTabTerm : public QMainWindow {
    Q_OBJECT

public:
    explicit qsshTabTerm(QWidget *parent = nullptr);

    /** Shows the Connect dialog if the user has not turned that off. */
    void showStartupDialog();

public slots:
    void openSession(SiteInfo);
    void openSession(int);
    void tabSelected(int);
    void changeTabIcon(QString termKey, SSHTermState state);

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;
    void closeEvent(QCloseEvent *event) override;

private:
    QTabWidget *tabs = nullptr;
    QSiteTreeDialog *connectDialog = nullptr;
    QDockWidget *managerDock = nullptr;
    QSessionManagerWidget *manager = nullptr;
    QLineEdit *quickConnect = nullptr;

    // actions that depend on having a session tab
    QList<QAction *> sessionActions;
    QAction *actSftp = nullptr;

    // status bar
    QLabel *statusMessage = nullptr;
    QLabel *statusCipher = nullptr;
    QLabel *statusHost = nullptr;
    QLabel *statusSize = nullptr;
    QLabel *statusTerm = nullptr;
    QLabel *statusClock = nullptr;

    void initActions();
    void initDock();
    void initStatusBar();
    void updateStatus();
    void updateSessionActions();

    void showConnectDialog();
    void showQuickConnectDialog();
    void quickConnectFromBox();
    void showSftpMgrDialog();
    void showFindDialog();
    void showAboutDialog();
    void on_tab_rightMouse_pressed(int clickedItem, QPoint position);
    QSSHTerm *termAt(int index) const;
    QSSHTerm *currentTerm() const;
    int indexOfTerm(const QString &termKey) const;

private slots:
    void closeTab(int);
};

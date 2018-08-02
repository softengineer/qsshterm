#include "qsshterm.h"
#include "qsiteconfig.h"
#include "qsshterm_sftp.h"
#include "qsshterm_common.h"
#include <QtWidgets>
#include <QApplication>
#include <QDebug>
#include <QMainWindow>
#include <QTabWidget>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QSpinBox>
#include <QLabel>
#include <QToolTip>


class qsshTabTerm: public QMainWindow {
	Q_OBJECT

	public:
	    qsshTabTerm(QWidget *parent = 0): QMainWindow(parent), tabs(0), sessionMgr_dialog(0), sftp_dialog(0), sftp_mgr(0) {
        init();
      }

      ~qsshTabTerm() {
      }

      void init();

  protected :
    bool eventFilter(QObject *obj, QEvent *ev);

  public slots:
      void openSession(SiteInfo);
      void openSession(int);
      void tabSelected(int);
      void changeTabIcon(QString index, SSHTermState state);
      void tabMoved(int from, int to);

  private:
      QTabWidget *tabs;
      QHash<QString, int> termKeyHash;
      QSiteTreeDialog *sessionMgr_dialog;
      QSSHTerm_Sftp_Dialog * sftp_dialog;
      QAction * sftp_mgr;
     	void initMenu();
     	void initToolbar();
     	void showSessionMgrDialog();  
      void showSftpMgrDialog();
      void showAboutDialog();
      void on_tab_rightMouse_pressed( int clickedItem, QPoint position );

  private slots:
      void closeTab(int);
};
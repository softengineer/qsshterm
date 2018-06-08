
#include "qsshtabterm.h"

int main(int argc, char *argv[])
{
    int height = 800;
    int width = 600;
    QApplication a(argc, argv);
    qsshTabTerm *window = new qsshTabTerm();
    window->resize(height, width);
    window->show();
    return a.exec();
   
    // QSSHTerm *term = new QSSHTerm();
    // int idx = tabs->addTab(term,"sshSession1");
    // term->setTabIndex(idx);
    // tabs->setTabIcon(idx, icon);
    
    // QSSHTerm *term1 = new QSSHTerm();
    // idx = tabs->addTab(term1,"sshSession2");
    // term1->setTabIndex(idx);
    // tabs->setTabIcon(idx, icon);
    // //term1->start();

    // QSSHTerm *term2 = new QSSHTerm();
    // idx = tabs->addTab(term2,"sshSession3");
    // term2->setTabIndex(idx);
    // tabs->setTabIcon(idx, icon);
    // //term2->start();

    // QSSHTerm *term3 = new QSSHTerm();
    // idx = tabs->addTab(term3,"sshSession4");
    // tabs->setTabIcon(idx, icon);
    // term3->setTabIndex(idx);
    // window->resize(height, width);
    // term->start();
    // term1->start();
    //term3->start();
    //tabs->addTab(new QWidget(),"TAB 2");
}
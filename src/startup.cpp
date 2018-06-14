
#include "qsshtabterm.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QIcon icon("./icon/qsshterm.ico"); 
    qsshTabTerm *window = new qsshTabTerm();
    QRect rect = QApplication::desktop()->screen()->rect();
    int width = rect.width();
    int height = rect.height();
    window->setWindowIcon(icon);
    window->resize(height -100, width -200);
    window->move(rect.center() - window->rect().center());
    window->show();
    return a.exec();
}
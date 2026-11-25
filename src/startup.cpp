
#include "qsshtabterm.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    // libssh >= 0.8 is thread safe once initialised; do it once for all sessions
    ssh_init();
    QIcon icon("./icon/qsshterm.ico");
    qsshTabTerm window;
    window.setWindowIcon(icon);
    window.resize(1024, 768);
    window.show();
    return a.exec();
}
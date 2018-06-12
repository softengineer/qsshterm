# qsshterm
Qt based cross platform SSH tab terminal

The ssh termnial with tab supporting work in Windows, Linux and Mac. This project is not standalone, it is based on other two opensource projects
1. qtermwidget - vt100 emulator
2. libssh - the ssh library to support ssh protocol.

The project is aimed to provide qt based GUI to offer easy way to access SSH server like putty or securecrt.

current version only works on Linux (I haven't tested on Mac, working on migrating to windows right now)
Compile:
1. Install qt5, openssh 
2. compile and install qtermwidget and libssh in linux
3. qmake qsshterm.pro
4. make


# qsshterm
Qt based cross platform SSH tab terminal

- support VT100 like terminal
- support multiple tab for each session
- support SFTP for ssh session (developing)
- tree session management

The ssh tab termnial aims at working in Windows, Linux and Mac. This project is not standalone, it is based on other two opensource projects
1. [qtermwidget](https://github.com/lxqt/qtermwidget) - vt100 emulator  
   Notice: please use qtermwidget to compile in 3rdparty directory since original one doesn't support Windows platform.
2. [libssh](https://www.libssh.org/) - the ssh library to support ssh protocol.

The project is aimed to provide qt based GUI to offer easy way to access SSH server like putty or securecrt.

current version only works on Linux, Windows (I haven't tested it on Mac)

Installation and compile:

In Linux :
1. Install qt5, openssh 
2. compile and install qtermwidget and libssh in linux
3. qmake qsshterm.pro
4. make

In Windows:
1. Install qt5 with mingw32
2. Install openssl
3. Install CMake
4. Compile qtermwidget and libssh
5. in qssshterm directory, mingw32-make


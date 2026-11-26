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

In Linux (Qt 6.5+, libssh 0.9+):

1. Install dependencies (Debian/Ubuntu):

       sudo apt install qt6-base-dev qt6-tools-dev cmake g++ libssh-dev

2. Build and install the bundled qtermwidget (Qt 6 port). To install without
   root, use a prefix such as ~/.local:

       cmake -S 3rdparty/qtermwidget -B build-qtw -DCMAKE_BUILD_TYPE=Release \
             -DCMAKE_INSTALL_PREFIX=$HOME/.local -DCMAKE_INSTALL_RPATH=$HOME/.local/lib
       cmake --build build-qtw -j
       cmake --install build-qtw

   (Omit the two prefix/rpath options and run the install with sudo to install
   into /usr/local instead.)

3. Build qsshterm:

       mkdir build-app && cd build-app
       qmake6 ../qsshterm.pro CONFIG+=release "INCLUDEPATH+=$HOME/.local/include" \
              "LIBS+=-L$HOME/.local/lib" "QMAKE_RPATHDIR+=$HOME/.local/lib"
       make -j
       cd ..

4. Install. qsshterm loads icon/ and config/sites.xml relative to the current
   directory, so install it together with those folders and start it through a
   small launcher:

       D=$HOME/.local/share/qsshterm
       mkdir -p $D/config $HOME/.local/bin
       cp build-app/qsshterm $D/
       cp -r icon color-schemes kb-layouts $D/
       [ -f $D/config/sites.xml ] || cp config/sites.xml $D/config/
       printf '#!/bin/sh\ncd "%s" && exec ./qsshterm "$@"\n' "$D" > $HOME/.local/bin/qsshterm
       chmod +x $HOME/.local/bin/qsshterm

   Then run `qsshterm` (make sure ~/.local/bin is on your PATH). Saved sites
   are stored in ~/.local/share/qsshterm/config/sites.xml.

   No root to install libssh-dev? If the libssh runtime (libssh-4) is already
   installed, matching headers are enough: extract the libssh-dev package
   (`apt download libssh-dev && dpkg-deb -x libssh-dev_*.deb libssh`), copy
   libssh/usr/include/libssh to ~/.local/include/, and link
   ~/.local/lib/libssh.so to /usr/lib/x86_64-linux-gnu/libssh.so.4.

Host keys are checked against ~/.ssh/known_hosts: unknown hosts are added on
first connect, and connections to hosts whose key has changed are refused.

Prebuilt packages: every push to master builds Windows (x64) and macOS
(Apple Silicon) packages with GitHub Actions (`.github/workflows/build.yml`);
download them from the run's Artifacts. Pushing a `v*` tag attaches them to a
GitHub release. The macOS app is unsigned: right-click > Open the first time.

Icons and the built-in icon themes (a Breeze subset, see `icons/README.md`)
are compiled into the executable, so the program no longer depends on the
working directory.

In Windows (the CI recipe uses MSYS2 UCRT64; see the workflow for exact steps):
1. Install Qt 6 (6.5 or newer) with mingw
2. Install openssl
3. Install CMake
4. Compile libssh and the bundled qtermwidget (3rdparty/qtermwidget) with CMake
5. in qsshterm directory, qmake6 qsshterm.pro && mingw32-make


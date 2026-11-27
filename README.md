# qsshterm

A Qt 6 based, cross-platform SSH terminal with tabs, in the spirit of PuTTY
and SecureCRT. It runs on Linux, Windows and macOS.

<img width="897" height="590" alt="qsshterm screenshot" src="https://github.com/user-attachments/assets/0629cf74-eff5-4de5-a34c-436653b950d7" />

## Features

- VT100/xterm-style terminal emulation with search, zoom and selectable color schemes
- Multiple sessions in tabs (duplicate, reconnect, close other tabs)
- Session manager that keeps saved sites in a folder tree, plus Quick Connect
- SFTP file manager for SSH sessions
- Jump hosts, SOCKS4/SOCKS5/HTTP proxies and ProxyCommand
- Local, remote and dynamic (`-L` / `-R` / `-D`) port forwarding, and agent forwarding
- Reads `~/.ssh/config`
- Light, dark and system UI themes

## Dependencies

qsshterm builds on two open-source projects:

1. [qtermwidget](https://github.com/lxqt/qtermwidget), the terminal emulator widget.
   Use the copy in `3rdparty/qtermwidget`. It is a Qt 6 port with Windows
   support, which the upstream version does not have.
2. [libssh](https://www.libssh.org/) 0.9 or newer, for the SSH protocol.

You also need Qt 6.5 or newer, including the Qt SVG module.

## Download

Every push to `master` builds packages for Windows (x64) and macOS (Apple
Silicon) with GitHub Actions (`.github/workflows/build.yml`). You can download
them from the Artifacts section of the workflow run. When a `v*` tag is
pushed, the packages are also attached to a GitHub release.

The macOS app is not signed. The first time you open it, right-click the app
and choose **Open**.

## Building on Linux

1. Install the dependencies (Debian/Ubuntu):

   ```sh
   sudo apt install qt6-base-dev libqt6svg6-dev qt6-tools-dev cmake g++ libssh-dev
   ```

2. Build and install the bundled qtermwidget. To install without root, use a
   prefix such as `~/.local`:

   ```sh
   cmake -S 3rdparty/qtermwidget -B build-qtw -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_INSTALL_PREFIX=$HOME/.local -DCMAKE_INSTALL_RPATH=$HOME/.local/lib
   cmake --build build-qtw -j
   cmake --install build-qtw
   ```

   To install into `/usr/local` instead, leave out the two prefix/rpath
   options and run the install step with `sudo`. The install also puts the
   color schemes and keyboard layouts under `<prefix>/share/qtermwidget6`,
   which is where qtermwidget looks for them.

3. Build qsshterm:

   ```sh
   mkdir build-app && cd build-app
   qmake6 ../qsshterm.pro CONFIG+=release "INCLUDEPATH+=$HOME/.local/include" \
          "LIBS+=-L$HOME/.local/lib" "QMAKE_RPATHDIR+=$HOME/.local/lib"
   make -j
   cd ..
   ```

4. Install the binary. Icons are compiled into the executable, so it does not
   need any files next to it:

   ```sh
   install -Dm755 build-app/qsshterm $HOME/.local/bin/qsshterm
   ```

   Then run `qsshterm`. Make sure `~/.local/bin` is on your `PATH`.

**No root access to install libssh-dev?** If the libssh runtime (`libssh-4`)
is already installed, you only need the matching headers. Extract the
libssh-dev package (`apt download libssh-dev && dpkg-deb -x libssh-dev_*.deb libssh`),
copy `libssh/usr/include/libssh` to `~/.local/include/`, and create a symlink
from `~/.local/lib/libssh.so` to `/usr/lib/x86_64-linux-gnu/libssh.so.4`.

## Building on Windows

The CI uses the [MSYS2](https://www.msys2.org/) UCRT64 toolchain. See
`.github/workflows/build.yml` for the exact steps, including packaging.

1. In an MSYS2 UCRT64 shell, install the toolchain and libraries:

   ```sh
   pacman -S --needed mingw-w64-ucrt-x86_64-{toolchain,cmake,ninja,qt6-base,qt6-svg,qt6-tools,libssh} make
   ```

2. Build the bundled qtermwidget:

   ```sh
   cmake -S 3rdparty/qtermwidget -B build-qtw -G Ninja \
         -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PWD/deps"
   cmake --build build-qtw && cmake --install build-qtw
   ```

3. Build qsshterm:

   ```sh
   mkdir build-app && cd build-app
   qmake6 ../qsshterm.pro CONFIG+=release \
          "INCLUDEPATH+=$PWD/../deps/include" "LIBS+=-L$PWD/../deps/lib"
   make -j
   ```

4. Copy the `color-schemes` and `kb-layouts` folders and
   `libqtermwidget6.dll` next to `qsshterm.exe`, then run `windeployqt6` on
   the executable.

## Building on macOS

```sh
brew install qt libssh ninja
cmake -S 3rdparty/qtermwidget -B build-qtw -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$(brew --prefix qt)" -DCMAKE_INSTALL_PREFIX="$PWD/deps"
cmake --build build-qtw && cmake --install build-qtw

mkdir build-app && cd build-app
"$(brew --prefix qt)/bin/qmake" ../qsshterm.pro CONFIG+=release \
      "INCLUDEPATH+=$PWD/../deps/include $(brew --prefix libssh)/include" \
      "LIBS+=-L$PWD/../deps/lib -L$(brew --prefix libssh)/lib"
make -j
```

To make a self-contained app, copy `color-schemes` and `kb-layouts` into
`qsshterm.app/Contents/Resources/` and run `macdeployqt` on the bundle, as
the CI does.

## Configuration and security

- Saved sessions are stored in `~/.qsshterm/sites.xml`. On first start, the
  file is created from `config/sites.xml` next to the program (or in the
  working directory), if that file exists.
- **Saved passwords are stored unencrypted** in `sites.xml`. Use key-based
  authentication or leave the password field empty, and qsshterm will
  prompt for the password when it connects.
- Host keys are checked against `~/.ssh/known_hosts`. Each site can set its
  own known_hosts file. You are asked to confirm unknown hosts. Connections to
  hosts whose key has changed are refused.

## License

LGPL-3.0. See [LICENSE](LICENSE). The bundled Breeze icons are LGPL-3.0-or-later
(see `icons/README.md`), and qtermwidget has its own license in `3rdparty/qtermwidget`.

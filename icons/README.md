# Bundled icons

`qsshterm/` and `qsshterm-dark/` are a subset of the KDE
[Breeze icon theme](https://invent.kde.org/frameworks/breeze-icons)
(`breeze` and `breeze-dark`), licensed under LGPL-3.0-or-later.

They are compiled into the executable (see `qsshterm.qrc`) and used as the
icon theme on Windows and macOS, and as the fallback for names missing from
the desktop icon theme on Linux. To add an icon, copy the 22px SVG of the
same name from both Breeze variants into the two `scalable/` folders and
list it in `qsshterm.qrc`.

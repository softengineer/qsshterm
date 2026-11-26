#-------------------------------------------------
#
# Project QSSHTerm V0.1
#  
#
# Author  : David.Fan
# License : LGPL
#
#-------------------------------------------------

QT       += core gui network xml widgets svg

lessThan(QT_MAJOR_VERSION, 6): error("QSSHTerm requires Qt 6")

TARGET = qsshterm
TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which as been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

CONFIG += c++17
#CONFIG += console

SOURCES += \
        src/startup.cpp \
    src/qsshtabterm.cpp \
    src/qsshterm.cpp \
    src/qsiteconfig.cpp \
    src/qssh_helpers.cpp \
    src/qssh_prompt.cpp \
    src/qssh_proxy.cpp \
    src/qssh_forward.cpp \
    src/qsite_editor.cpp \
    src/qssh_theme.cpp \
    src/qsshterm_sftp.cpp
    
HEADERS += \
    src/qsshtabterm.h \
    src/qsshterm.h \
    src/qsiteconfig.h \
    src/qssh_utils.h \
    src/qssh_helpers.h \
    src/qssh_prompt.h \
    src/qssh_proxy.h \
    src/qssh_forward.h \
    src/qsite_editor.h \
    src/qssh_icons.h \
    src/qssh_theme.h \
    src/qsshterm_sftp.h

# libssh >= 0.8 has thread support built in; libssh_threads no longer exists
LIBS += -lqtermwidget6
LIBS += -lssh
win32: LIBS += -lws2_32

release: DESTDIR = ./
         TEMPDIR = build/release
debug:   DESTDIR = ./
         TEMPDIR = build/debug
OBJECTS_DIR = $$TEMPDIR/.obj
MOC_DIR = $$TEMPDIR/.moc
RCC_DIR = $$TEMPDIR/.qrc
UI_DIR = $$TEMPDIR/.ui

# icons and bundled icon themes are compiled in: no files needed next to the binary
RESOURCES += qsshterm.qrc

win32:RC_ICONS += icon/qsshterm.ico


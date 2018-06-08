#-------------------------------------------------
#
# Project QSSHTerm V0.1
#  
#
# Author  : David.Fan
# License : LGPL
#
#-------------------------------------------------

QT       += core gui network xml

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

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

CONFIG += c++11

SOURCES += \
        src/startup.cpp \
    src/qsshtabterm.cpp \
    src/qsshterm.cpp \
    src/qsiteconfig.cpp
    
HEADERS += \
	qsshtabterm.h \
    src/qsshterm.h \
    src/qsshtabterm.h \
    src/qsiteconfig.h \
    src/qssh_utils.h

LIBS += -lqtermwidget5
LIBS += -lssh
LIBS += -lssh_threads
/*
 * This file is a part of QTerminal - http://gitorious.org/qterminal
 *
 * This file was un-linked from KDE and modified
 * by Maxim Bourmistrov <maxim@unixconn.com>
 *
 */

/*

   This file is part of the KDE libraries
   Copyright (C) 2007 Oswald Buddenhagen <ossi@kde.org>
   Copyright (C) 2010 KDE e.V. <kde-ev-board@kde.org>
     Author Adriaan de Groot <groot@kde.org>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#include "kptydevice.h"
#include "kpty_p.h"

#include <QSocketNotifier>

#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <fcntl.h>

#ifndef __WIN32__
#include <sys/ioctl.h>
#endif

#ifdef HAVE_SYS_FILIO_H
# include <sys/filio.h>
#endif
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif

#if defined(Q_OS_FREEBSD) || defined(Q_OS_MAC)
  // "the other end's output queue size" - kinda braindead, huh?
# define PTY_BYTES_AVAILABLE TIOCOUTQ
#elif defined(TIOCINQ)
  // "our end's input queue size"
# define PTY_BYTES_AVAILABLE TIOCINQ
#else
  // likewise. more generic ioctl (theoretically)
# define PTY_BYTES_AVAILABLE FIONREAD
#endif




//////////////////
// private data //
//////////////////

// Lifted from Qt. I don't think they would mind. ;)
// Re-lift again from Qt whenever a proper replacement for pthread_once appears
static void qt_ignore_sigpipe()
{
   }

#define NO_INTR(ret,func) do { ret = func; } while (ret < 0 && errno == EINTR)

bool KPtyDevicePrivate::_k_canRead()
{
   
        return true;

}

bool KPtyDevicePrivate::_k_canWrite()
{
    
    return true;
}

#ifndef timeradd
// Lifted from GLIBC
# define timeradd(a, b, result) \
    do { \
        (result)->tv_sec = (a)->tv_sec + (b)->tv_sec; \
        (result)->tv_usec = (a)->tv_usec + (b)->tv_usec; \
        if ((result)->tv_usec >= 1000000) { \
            ++(result)->tv_sec; \
            (result)->tv_usec -= 1000000; \
        } \
    } while (0)
# define timersub(a, b, result) \
    do { \
        (result)->tv_sec = (a)->tv_sec - (b)->tv_sec; \
        (result)->tv_usec = (a)->tv_usec - (b)->tv_usec; \
        if ((result)->tv_usec < 0) { \
            --(result)->tv_sec; \
            (result)->tv_usec += 1000000; \
        } \
    } while (0)
#endif

bool KPtyDevicePrivate::doWait(int msecs, bool reading)
{
    
    return false;
}

void KPtyDevicePrivate::finishOpen(QIODevice::OpenMode mode)
{
   
}

/////////////////////////////
// public member functions //
/////////////////////////////

KPtyDevice::KPtyDevice(QObject *parent) :
    QIODevice(parent),
    KPty(new KPtyDevicePrivate(this))
{
}

KPtyDevice::~KPtyDevice()
{
    close();
}

bool KPtyDevice::open(OpenMode mode)
{
    

    return true;
}

bool KPtyDevice::open(int fd, OpenMode mode)
{
    
    return true;
}

void KPtyDevice::close()
{
    
}

bool KPtyDevice::isSequential() const
{
    return true;
}

bool KPtyDevice::canReadLine() const
{
  return true;
}

bool KPtyDevice::atEnd() const
{
     return true;
}

qint64 KPtyDevice::bytesAvailable() const
{
     return 0;
}

qint64 KPtyDevice::bytesToWrite() const
{
   return 0;
}

bool KPtyDevice::waitForReadyRead(int msecs)
{
  return 0;
}

bool KPtyDevice::waitForBytesWritten(int msecs)
{
   return false;
}

void KPtyDevice::setSuspended(bool suspended)
{
}

bool KPtyDevice::isSuspended() const
{
   return true;
}

// protected
qint64 KPtyDevice::readData(char *data, qint64 maxlen)
{
    return 0;
}

// protected
qint64 KPtyDevice::readLineData(char *data, qint64 maxlen)
{
    return 0;
}

// protected
qint64 KPtyDevice::writeData(const char *data, qint64 len)
{
   return 0;
}

/*

   This file is part of the KDE libraries
   Copyright (C) 2002 Waldo Bastian <bastian@kde.org>
   Copyright (C) 2002-2003,2007 Oswald Buddenhagen <ossi@kde.org>

    Rewritten for QT4 by e_k <e_k at users.sourceforge.net>, Copyright (C)2008

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

#include "kpty_p.h"

#include <QtDebug>


#if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
#define HAVE_LOGIN
#define HAVE_LIBUTIL_H
#endif

#ifdef __sgi
#define __svr4__
#endif

#ifdef __osf__
#define _OSF_SOURCE
#include <float.h>
#endif

#ifdef _AIX
#define _ALL_SOURCE
#endif

// __USE_XOPEN isn't defined by default in ICC
// (needed for ptsname(), grantpt() and unlockpt())
#ifdef __INTEL_COMPILER
#  ifndef __USE_XOPEN
#    define __USE_XOPEN
#  endif
#endif

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/param.h>

#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <grp.h>

#if defined(HAVE_PTY_H)
# include <pty.h>
#endif

#ifdef HAVE_LIBUTIL_H
# include <libutil.h>
#elif defined(HAVE_UTIL_H)
# include <util.h>
#endif

#ifdef HAVE_UTEMPTER
extern "C" {
# include <utempter.h>
}
#else
# include <utmp.h>
# ifdef HAVE_UTMPX
#  include <utmpx.h>
# endif
# if !defined(_PATH_UTMPX) && defined(_UTMPX_FILE)
#  define _PATH_UTMPX _UTMPX_FILE
# endif
# ifdef HAVE_UPDWTMPX
#  if !defined(_PATH_WTMPX) && defined(_WTMPX_FILE)
#   define _PATH_WTMPX _WTMPX_FILE
#  endif
# endif
#endif

/* for HP-UX (some versions) the extern C is needed, and for other
   platforms it doesn't hurt */
extern "C" {
#include <termios.h>
#if defined(HAVE_TERMIO_H)
# include <termio.h> // struct winsize on some systems
#endif
}

#if defined (_HPUX_SOURCE)
# define _TERMIOS_INCLUDED
# include <bsdtty.h>
#endif

#ifdef HAVE_SYS_STROPTS_H
# include <sys/stropts.h> // Defines I_PUSH
# define _NEW_TTY_CTRL
#endif

#if defined (__FreeBSD__) || defined(__FreeBSD_kernel__) || defined (__NetBSD__) || defined (__OpenBSD__) || defined (__bsdi__) || defined(__APPLE__) || defined (__DragonFly__)
# define _tcgetattr(fd, ttmode) ioctl(fd, TIOCGETA, (char *)ttmode)
#else
# if defined(_HPUX_SOURCE) || defined(__Lynx__) || defined (__CYGWIN__) || defined(__GNU__)
#  define _tcgetattr(fd, ttmode) tcgetattr(fd, ttmode)
# else
#  define _tcgetattr(fd, ttmode) ioctl(fd, TCGETS, (char *)ttmode)
# endif
#endif

#if defined (__FreeBSD__) || defined(__FreeBSD_kernel__) || defined (__NetBSD__) || defined (__OpenBSD__) || defined (__bsdi__) || defined(__APPLE__) || defined (__DragonFly__)
# define _tcsetattr(fd, ttmode) ioctl(fd, TIOCSETA, (char *)ttmode)
#else
# if defined(_HPUX_SOURCE) || defined(__CYGWIN__) || defined(__GNU__)
#  define _tcsetattr(fd, ttmode) tcsetattr(fd, TCSANOW, ttmode)
# else
#  define _tcsetattr(fd, ttmode) ioctl(fd, TCSETS, (char *)ttmode)
# endif
#endif

//#include <kdebug.h>
//#include <kstandarddirs.h>  // findExe

// not defined on HP-UX for example
#ifndef CTRL
# define CTRL(x) ((x) & 037)
#endif

#define TTY_GROUP "tty"

///////////////////////
// private functions //
///////////////////////

//////////////////
// private data //
//////////////////

KPtyPrivate::KPtyPrivate(KPty* parent) :
        masterFd(-1), slaveFd(-1), ownMaster(true), q_ptr(parent)
{
}

KPtyPrivate::~KPtyPrivate()
{
}

bool KPtyPrivate::chownpty(bool)
{
//    return !QProcess::execute(KStandardDirs::findExe("kgrantpty"),
//        QStringList() << (grant?"--grant":"--revoke") << QString::number(masterFd));
    return true;
}

/////////////////////////////
// public member functions //
/////////////////////////////

KPty::KPty() :
        d_ptr(new KPtyPrivate(this))
{
}

KPty::KPty(KPtyPrivate *d) :
        d_ptr(d)
{
    d_ptr->q_ptr = this;
}

KPty::~KPty()
{
    close();
    delete d_ptr;
}

bool KPty::open()
{
   


    return true;
}

bool KPty::open(int fd)
{

    return true;
}

void KPty::closeSlave()
{
 
}

bool KPty::openSlave()
{
    
    return true;
}

void KPty::close()
{
   
}

void KPty::setCTty()
{
    
}

void KPty::logout()
{

}

// XXX Supposedly, tc[gs]etattr do not work with the master on Solaris.
// Please verify.

bool KPty::tcGetAttr(struct ::termios * ttmode) const
{
   return true;
}

bool KPty::tcSetAttr(struct ::termios * ttmode)
{
     return true;
}

bool KPty::setWinSize(int lines, int columns)
{
     return true;
}

bool KPty::setEcho(bool echo)
{
     return true;
}

const char * KPty::ttyName() const
{
     return "";
}

int KPty::masterFd() const
{
  return 0;
}

int KPty::slaveFd() const
{
    return 0;
}

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


#include "kptyprocess.h"
#include "kprocess.h"
#include "kptydevice.h"

#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <QDebug>

KPtyProcess::KPtyProcess(QObject *parent) :
    KProcess(new KPtyProcessPrivate, parent)
{
    }

KPtyProcess::KPtyProcess(int ptyMasterFd, QObject *parent) :
    KProcess(new KPtyProcessPrivate, parent)
{
   ;
}

KPtyProcess::~KPtyProcess()
{
   
}

void KPtyProcess::setPtyChannels(PtyChannels channels)
{
    Q_D(KPtyProcess);

    d->ptyChannels = channels;
}

KPtyProcess::PtyChannels KPtyProcess::ptyChannels() const
{
    return NULL;
}

void KPtyProcess::setUseUtmp(bool value)
{
   return;
}

bool KPtyProcess::isUseUtmp() const
{
   return true;
}

KPtyDevice *KPtyProcess::pty() const
{
    return 0;
}

void KPtyProcess::setupChildProcess()
{
    Q_D(KPtyProcess);

    d->pty->setCTty();

#if 0
    if (d->addUtmp)
        d->pty->login(KUser(KUser::UseRealUserID).loginName().toLocal8Bit().data(), qgetenv("DISPLAY"));
#endif
    if (d->ptyChannels & StdinChannel)
        dup2(d->pty->slaveFd(), 0);

    if (d->ptyChannels & StdoutChannel)
        dup2(d->pty->slaveFd(), 1);

    if (d->ptyChannels & StderrChannel)
        dup2(d->pty->slaveFd(), 2);

    KProcess::setupChildProcess();
}

//#include "kptyprocess.moc"

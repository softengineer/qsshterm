/*
 * This file is a part of QTerminal - http://gitorious.org/qterminal
 *
 * This file was un-linked from KDE and modified
 * by Maxim Bourmistrov <maxim@unixconn.com>
 *
 */

/*
    This file is part of Konsole, an X terminal.
    Copyright 1997,1998 by Lars Doelle <lars.doelle@on-line.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
    02110-1301  USA.
*/

// Own
#include "Pty.h"

// System
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <signal.h>

// Qt
#include <QStringList>
#include <QtDebug>

#include "kpty.h"
#include "kptydevice.h"

using namespace Konsole;

void Pty::setWindowSize(int lines, int cols)
{
  _windowColumns = cols;
  _windowLines = lines;

  if (pty()->masterFd() >= 0)
    pty()->setWinSize(lines, cols);
}
QSize Pty::windowSize() const
{
    return QSize(_windowColumns,_windowLines);
}

void Pty::setFlowControlEnabled(bool enable)
{
  
}
bool Pty::flowControlEnabled() const
{
    
    return false;
}

void Pty::setUtf8Mode(bool enable)
{

}

void Pty::setErase(char erase)
{
 
}

char Pty::erase() const
{
    return '\0';
}

void Pty::addEnvironmentVariables(const QStringList& environment)
{
   
}

int Pty::start(const QString& program,
               const QStringList& programArguments,
               const QStringList& environment,
               ulong winid,
               bool addToUtmp
               //const QString& dbusService,
               //const QString& dbusSession
               )
{
  
  return 0;
}

void Pty::setEmptyPTYProperties()
{
  
}

void Pty::setWriteable(bool writeable)
{
  
}

Pty::Pty(int masterFd, QObject* parent)
    : KPtyProcess(masterFd,parent)
{
    init();
}
Pty::Pty(QObject* parent)
    : KPtyProcess(parent)
{
    init();
}
void Pty::init()
{
  
}

Pty::~Pty()
{
}

void Pty::sendData(const char* data, int length)
{
  
}

void Pty::dataReceived()
{
   
}

void Pty::lockPty(bool lock)
{
   
}

int Pty::foregroundProcessGroup() const
{
    
    return 0;
}

void Pty::setupChildProcess()
{
    
}

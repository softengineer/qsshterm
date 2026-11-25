#ifndef SSHTERM_COMMON_H
#define SSHTERM_COMMON_H

#include <QString>
#include <QMetaType>

struct SiteInfo {
    QString sitename;
    QString hostname;
    QString username;
    QString password;
    QString privateKeyPath;

    int port;
    bool transferX11;

    SiteInfo() : sitename(QString()), 
    hostname(QString()), 
    username(QString()), 
    password(QString()), 
    privateKeyPath(QString()), 
    port(-1), 
    transferX11(false) {
        
    }
};

Q_DECLARE_METATYPE(SiteInfo)


enum SSHTermState
{
    DISCONNECT,
    IDLE ,
    BUSY
};

struct TreeNode {
    bool isFolder;
    SiteInfo  siteInfo;

    TreeNode(): isFolder(true) {
    }
};

Q_DECLARE_METATYPE(TreeNode)

#endif
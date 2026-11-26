#ifndef SSHTERM_COMMON_H
#define SSHTERM_COMMON_H

#include <QString>
#include <QMetaType>
#include <QList>
#include <QPair>

/** One -L / -R / -D style port forward of a site. */
struct PortForward {
    enum Type { Local, Remote, Dynamic };
    Type type = Local;
    QString bindAddress;   // empty: localhost (Local/Dynamic), all interfaces chosen by server (Remote)
    int bindPort = 0;
    QString destHost;      // unused for Dynamic
    int destPort = 0;      // unused for Dynamic
};

/** One hop of a jump host chain, resolved from a saved session or user@host:port. */
struct JumpHost {
    QString label;          // what the user typed, for messages
    QString hostname;
    QString username;
    int port = 22;
    QString password;
    QString privateKeyPath;
    QString certificatePath;
};

/**
 * Settings of one saved session (sites.xml). Fields missing from the file
 * keep these defaults, which match the behaviour before the option existed
 * (except unknown host keys, which are now confirmed with the user).
 */
struct SiteInfo {
    // --- connection
    QString sitename;
    QString hostname;
    QString username;
    QString password;
    QString privateKeyPath;
    int port = -1;
    /** comma separated hops: saved session names or [user@]host[:port] */
    QString jumpHosts;
    enum ProxyType { NoProxy, Socks5, Socks4, Http, ProxyCommand };
    int proxyType = NoProxy;
    QString proxyHost;
    int proxyPort = 0;
    QString proxyUser;
    QString proxyPassword;
    QString proxyCommand;
    int connectTimeout = 0;      // seconds, 0: libssh default
    int keepAliveInterval = 0;   // seconds, 0: off
    enum AddressFamily { AnyFamily, IPv4, IPv6 };
    int addressFamily = AnyFamily;
    QString bindAddress;
    bool useSshConfig = true;
    QString sftpStartDir;

    // --- authentication
    bool authPublicKey = true;
    bool authAgent = true;
    bool authPassword = true;
    bool authKeyboardInteractive = true;
    bool authGssapi = true;
    bool gssapiDelegate = false;
    QString certificatePath;
    QString agentSocket;          // empty: SSH_AUTH_SOCK
    bool savePasswordOnPrompt = false;

    // --- forwarding
    bool transferX11 = false;
    bool forwardAgent = false;
    QList<PortForward> forwards;

    // --- terminal
    QString termType;             // empty: xterm
    QString encoding;             // empty: UTF-8; "Latin1", "System"
    QString colorScheme;          // empty: follow the app theme
    QString fontFamily;           // empty: default monospace
    int fontSize = 0;             // 0: default
    int scrollback = -2;          // -2: default (1000), -1: unlimited
    QString keyboardLayout;       // empty: qtermwidget default
    QString remoteCommand;        // empty: login shell
    QString startupCommands;      // sent line by line after login
    QList<QPair<QString, QString>> environment;

    // --- advanced
    int compression = -1;         // -1: ssh config / libssh default, 0 no, 1 yes
    QString ciphers;
    QString kexAlgorithms;
    QString macs;
    QString hostKeyAlgorithms;
    QString pubkeyAcceptedTypes;
    enum HostKeyCheck { HostKeyAsk, HostKeyAcceptNew, HostKeyStrict, HostKeyOff };
    int hostKeyCheck = HostKeyAsk;
    QString knownHostsFile;       // empty: ~/.ssh/known_hosts
    int logLevel = 1;             // SSH_LOG_WARNING

    // --- runtime only, never saved
    QList<JumpHost> jumpChain;    // jumpHosts resolved by SiteTree::resolveJumpHosts
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
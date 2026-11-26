#ifndef QSSH_FORWARD_H
#define QSSH_FORWARD_H

#include <libssh/libssh.h>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QPointer>

#include "qsshterm_common.h"

class QIODevice;
class QTcpServer;
class QTcpSocket;

namespace qssh {

class Connection;

/**
 * Copies data both ways between one SSH channel and a local socket (TCP,
 * Unix socket or named pipe). Lives on the session thread; channel data is
 * moved by pump(), which the session calls whenever its socket is readable.
 */
class ChannelBridge : public QObject
{
    Q_OBJECT
public:
    /** Takes ownership of @p channel and @p device. */
    ChannelBridge(ssh_channel channel, QIODevice *device, QObject *parent = nullptr);
    ~ChannelBridge() override;

    /** Channel -> device. Returns the bytes moved, -1 once the bridge is done. */
    int pump();
    /** Bytes received on the device before the bridge existed (e.g. after a SOCKS request). */
    void writeToChannel(const QByteArray &data);

signals:
    /** Something was written to the channel; libssh may have buffered input meanwhile. */
    void wrote();

private slots:
    void onReadyRead();
    void onDeviceClosed();

private:
    void finish(bool sendEof);

    ssh_channel channel;
    QIODevice *device;
    bool done = false;
    char buffer[32 * 1024];
};

/**
 * X11, agent and port forwarding (-L, -R, -D) of one terminal session.
 */
class ForwardManager : public QObject
{
    Q_OBJECT
public:
    ForwardManager(Connection *connection, const SiteInfo &site, QObject *parent = nullptr);
    ~ForwardManager() override;

    /** Registers the channel callbacks on the connection; call before Connection::open(). */
    void install();
    /** After authentication: local listeners and remote forward requests. */
    void start();
    /** X11 / agent requests on the shell channel, before the shell or command starts. */
    void setupShellChannel(ssh_channel shell);
    /** Closes all forwarded channels and listeners; call before the session is freed. */
    void stop();
    /** Moves pending channel data to the local sockets; true if anything moved. */
    bool pump();

    /** Opens a direct-tcpip channel (used by local and SOCKS forwards). */
    ssh_channel openDirect(const QString &host, int port, const QString &originHost, int originPort);
    void addBridge(ssh_channel channel, QIODevice *device, const QByteArray &pending = QByteArray());
    void report(const QString &text) { emit notice(text); }

signals:
    void notice(const QString &text);
    /** Data was written to a channel; the session should look for input. */
    void wake();

private:
    ssh_channel openX11Channel();
    ssh_channel openAgentChannel();
    ssh_channel openForwardedChannel(int boundPort);
    void listenLocal(const PortForward &forward);
    void acceptLocal(QTcpServer *server, PortForward forward);
    QString agentPath() const;

    Connection *connection;
    SiteInfo site;
    bool x11Active = false;
    bool agentActive = false;
    QList<QPointer<ChannelBridge>> bridges;
    QList<QTcpServer *> servers;
    QHash<int, QPair<QString, int>> remoteTargets;   // bound remote port -> local destination
};

} // namespace qssh

#endif

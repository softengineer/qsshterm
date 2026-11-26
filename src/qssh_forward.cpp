#include "qssh_forward.h"
#include "qssh_helpers.h"

#include <QDir>
#include <QHostAddress>
#include <QLocalSocket>
#include <QProcess>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>

namespace qssh {

// Closes @p device once its pending output is written, then deletes it.
static void closeDevice(QIODevice *device)
{
    if (!device)
        return;
    device->setParent(nullptr);
    if (QAbstractSocket *socket = qobject_cast<QAbstractSocket *>(device)) {
        QObject::connect(socket, &QAbstractSocket::disconnected, socket, &QObject::deleteLater);
        socket->disconnectFromHost();
        if (socket->state() == QAbstractSocket::UnconnectedState)
            socket->deleteLater();
    } else if (QLocalSocket *socket = qobject_cast<QLocalSocket *>(device)) {
        QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        socket->disconnectFromServer();
        if (socket->state() == QLocalSocket::UnconnectedState)
            socket->deleteLater();
    } else {
        device->close();
        device->deleteLater();
    }
}

static QHostAddress listenAddress(const QString &bind)
{
    if (bind.isEmpty() || bind == QLatin1String("localhost"))
        return QHostAddress(QHostAddress::LocalHost);
    if (bind == QLatin1String("*") || bind == QLatin1String("0.0.0.0"))
        return QHostAddress(QHostAddress::Any);
    return QHostAddress(bind);
}

// ---------------------------------------------------------------------------
// ChannelBridge

ChannelBridge::ChannelBridge(ssh_channel ch, QIODevice *dev, QObject *parent)
    : QObject(parent), channel(ch), device(dev)
{
    device->setParent(this);
    connect(device, &QIODevice::readyRead, this, &ChannelBridge::onReadyRead);
    if (QAbstractSocket *socket = qobject_cast<QAbstractSocket *>(device)) {
        connect(socket, &QAbstractSocket::disconnected, this, &ChannelBridge::onDeviceClosed);
        connect(socket, &QAbstractSocket::errorOccurred, this, [this, socket](QAbstractSocket::SocketError e) {
            // the peer closing its side is reported as an error too
            if (e != QAbstractSocket::RemoteHostClosedError || socket->state() == QAbstractSocket::UnconnectedState)
                onDeviceClosed();
        });
    } else if (QLocalSocket *socket = qobject_cast<QLocalSocket *>(device)) {
        connect(socket, &QLocalSocket::disconnected, this, &ChannelBridge::onDeviceClosed);
    }
}

ChannelBridge::~ChannelBridge()
{
    if (channel) {
        ssh_channel_close(channel);
        ssh_channel_free(channel);
        channel = nullptr;
    }
}

int ChannelBridge::pump()
{
    if (done || !channel)
        return -1;
    int moved = 0;
    int n = 0;
    while ((n = ssh_channel_read_nonblocking(channel, buffer, sizeof(buffer), 0)) > 0) {
        device->write(buffer, n);
        moved += n;
    }
    if (n == SSH_ERROR || ssh_channel_is_eof(channel) || ssh_channel_is_closed(channel)) {
        finish(false);
        return moved > 0 ? moved : -1;
    }
    return moved;
}

void ChannelBridge::writeToChannel(const QByteArray &data)
{
    if (done || !channel || data.isEmpty())
        return;
    qsizetype written = 0;
    while (written < data.size()) {
        const int n = ssh_channel_write(channel, data.constData() + written, uint32_t(data.size() - written));
        if (n <= 0) {
            finish(false);
            return;
        }
        written += n;
    }
    emit wrote();
}

void ChannelBridge::onReadyRead()
{
    writeToChannel(device->readAll());
}

void ChannelBridge::onDeviceClosed()
{
    if (done)
        return;
    writeToChannel(device->readAll());
    finish(true);
}

void ChannelBridge::finish(bool sendEof)
{
    if (done)
        return;
    done = true;
    if (channel) {
        if (sendEof)
            ssh_channel_send_eof(channel);
        ssh_channel_close(channel);
        ssh_channel_free(channel);
        channel = nullptr;
    }
    QIODevice *dev = device;
    device = nullptr;
    disconnect(dev, nullptr, this, nullptr);
    closeDevice(dev);
    deleteLater();
}

// ---------------------------------------------------------------------------
// SOCKS4/4a/5 server side, for dynamic forwarding

namespace {

class SocksClient : public QObject
{
public:
    SocksClient(QTcpSocket *socket, ForwardManager *manager)
        : QObject(manager), socket(socket), manager(manager)
    {
        socket->setParent(this);
        connect(socket, &QTcpSocket::readyRead, this, [this]() { onRead(); });
        connect(socket, &QTcpSocket::disconnected, this, [this]() { deleteLater(); });
        onRead();
    }

private:
    void fail(const QByteArray &reply)
    {
        if (!reply.isEmpty())
            socket->write(reply);
        disconnect(socket, nullptr, this, nullptr);
        closeDevice(socket);
        socket = nullptr;
        deleteLater();
    }

    void onRead()
    {
        if (!socket)
            return;
        buffer += socket->readAll();
        if (buffer.isEmpty())
            return;
        if (buffer.at(0) == 4)
            socks4();
        else if (buffer.at(0) == 5)
            socks5();
        else
            fail(QByteArray());
    }

    void socks4()
    {
        if (buffer.size() < 9)
            return;
        const int userEnd = buffer.indexOf('\0', 8);
        if (userEnd < 0)
            return;
        const int port = (quint8(buffer.at(2)) << 8) | quint8(buffer.at(3));
        const quint32 ip = (quint32(quint8(buffer.at(4))) << 24) | (quint8(buffer.at(5)) << 16)
                           | (quint8(buffer.at(6)) << 8) | quint8(buffer.at(7));
        QString host;
        int end = userEnd + 1;
        if (ip > 0 && ip < 256) {   // SOCKS4a: host name follows the user id
            const int hostEnd = buffer.indexOf('\0', end);
            if (hostEnd < 0)
                return;
            host = QString::fromUtf8(buffer.mid(end, hostEnd - end));
            end = hostEnd + 1;
        } else {
            host = QHostAddress(ip).toString();
        }
        const QByteArray refused("\x00\x5b\x00\x00\x00\x00\x00\x00", 8);
        if (buffer.at(1) != 1)
            return fail(refused);
        connectTarget(host, port, buffer.mid(end), QByteArray("\x00\x5a\x00\x00\x00\x00\x00\x00", 8), refused);
    }

    void socks5()
    {
        if (!greeted) {
            if (buffer.size() < 2 || buffer.size() < 2 + quint8(buffer.at(1)))
                return;
            const QByteArray methods = buffer.mid(2, quint8(buffer.at(1)));
            buffer.remove(0, 2 + methods.size());
            if (!methods.contains('\0'))
                return fail(QByteArray("\x05\xff", 2));
            socket->write(QByteArray("\x05\x00", 2));
            greeted = true;
        }
        if (buffer.size() < 5)
            return;
        int length = 0;
        QString host;
        switch (buffer.at(3)) {
        case 1:
            length = 4 + 4 + 2;
            if (buffer.size() < length)
                return;
            host = QHostAddress((quint32(quint8(buffer.at(4))) << 24) | (quint8(buffer.at(5)) << 16)
                                | (quint8(buffer.at(6)) << 8) | quint8(buffer.at(7))).toString();
            break;
        case 3:
            length = 4 + 1 + quint8(buffer.at(4)) + 2;
            if (buffer.size() < length)
                return;
            host = QString::fromUtf8(buffer.mid(5, quint8(buffer.at(4))));
            break;
        case 4: {
            length = 4 + 16 + 2;
            if (buffer.size() < length)
                return;
            Q_IPV6ADDR v6;
            memcpy(v6.c, buffer.constData() + 4, 16);
            host = QHostAddress(v6).toString();
            break;
        }
        default:
            return fail(QByteArray("\x05\x08\x00\x01\x00\x00\x00\x00\x00\x00", 10));
        }
        const int port = (quint8(buffer.at(length - 2)) << 8) | quint8(buffer.at(length - 1));
        if (buffer.at(1) != 1)   // only CONNECT
            return fail(QByteArray("\x05\x07\x00\x01\x00\x00\x00\x00\x00\x00", 10));
        connectTarget(host, port, buffer.mid(length),
                      QByteArray("\x05\x00\x00\x01\x00\x00\x00\x00\x00\x00", 10),
                      QByteArray("\x05\x05\x00\x01\x00\x00\x00\x00\x00\x00", 10));
    }

    void connectTarget(const QString &host, int port, const QByteArray &rest,
                       const QByteArray &okReply, const QByteArray &failReply)
    {
        ssh_channel channel = manager->openDirect(host, port, socket->peerAddress().toString(), socket->peerPort());
        if (!channel)
            return fail(failReply);
        socket->write(okReply);
        disconnect(socket, nullptr, this, nullptr);
        QTcpSocket *s = socket;
        socket = nullptr;
        manager->addBridge(channel, s, rest);
        deleteLater();
    }

    QTcpSocket *socket;
    ForwardManager *manager;
    QByteArray buffer;
    bool greeted = false;
};

} // namespace

// ---------------------------------------------------------------------------
// ForwardManager

ForwardManager::ForwardManager(Connection *conn, const SiteInfo &info, QObject *parent)
    : QObject(parent), connection(conn), site(info)
{
}

ForwardManager::~ForwardManager()
{
    stop();
}

void ForwardManager::install()
{
    if (site.transferX11)
        connection->onX11 = [this](const char *, int) { return openX11Channel(); };
    if (site.forwardAgent)
        connection->onAgent = [this]() { return openAgentChannel(); };
    bool remote = false;
    for (const PortForward &f : site.forwards)
        remote |= f.type == PortForward::Remote;
    if (remote)
        connection->onForwardedTcpip = [this](const char *, int boundPort, const char *, int) {
            return openForwardedChannel(boundPort);
        };
}

void ForwardManager::start()
{
    ssh_session session = connection->session();
    if (!session)
        return;
    for (const PortForward &f : site.forwards) {
        if (f.type == PortForward::Remote) {
            const QByteArray bind = (f.bindAddress.isEmpty() ? QStringLiteral("localhost") : f.bindAddress).toUtf8();
            int bound = 0;
            if (ssh_channel_listen_forward(session, bind.constData(), f.bindPort, &bound) != SSH_OK) {
                emit notice(tr("Remote forward %1:%2 refused by the server: %3")
                                .arg(QString::fromUtf8(bind)).arg(f.bindPort)
                                .arg(QString::fromUtf8(ssh_get_error(session))));
                continue;
            }
            const int port = bound > 0 ? bound : f.bindPort;
            remoteTargets.insert(port, qMakePair(f.destHost, f.destPort));
            emit notice(tr("Forwarding remote %1:%2 to %3:%4")
                            .arg(QString::fromUtf8(bind)).arg(port).arg(f.destHost).arg(f.destPort));
        } else {
            listenLocal(f);
        }
    }
}

void ForwardManager::listenLocal(const PortForward &forward)
{
    const QHostAddress address = listenAddress(forward.bindAddress);
    const QString where = QStringLiteral("%1:%2")
                              .arg(forward.bindAddress.isEmpty() ? QStringLiteral("localhost") : forward.bindAddress)
                              .arg(forward.bindPort);
    QTcpServer *server = new QTcpServer(this);
    if (address.isNull() || !server->listen(address, quint16(forward.bindPort))) {
        emit notice(tr("Cannot listen on %1: %2")
                        .arg(where, address.isNull() ? tr("invalid address") : server->errorString()));
        delete server;
        return;
    }
    servers << server;
    connect(server, &QTcpServer::newConnection, this, [this, server, forward]() { acceptLocal(server, forward); });
    if (forward.type == PortForward::Dynamic)
        emit notice(tr("SOCKS proxy listening on %1").arg(where));
    else
        emit notice(tr("Forwarding local %1 to %2:%3").arg(where, forward.destHost).arg(forward.destPort));
}

void ForwardManager::acceptLocal(QTcpServer *server, PortForward forward)
{
    while (QTcpSocket *socket = server->nextPendingConnection()) {
        if (forward.type == PortForward::Dynamic) {
            new SocksClient(socket, this);
            continue;
        }
        ssh_channel channel = openDirect(forward.destHost, forward.destPort,
                                         socket->peerAddress().toString(), socket->peerPort());
        if (!channel) {
            closeDevice(socket);
            continue;
        }
        addBridge(channel, socket, socket->readAll());
    }
}

ssh_channel ForwardManager::openDirect(const QString &host, int port, const QString &originHost, int originPort)
{
    ssh_session session = connection->session();
    if (!session)
        return nullptr;
    ssh_channel channel = ssh_channel_new(session);
    if (!channel)
        return nullptr;
    const QByteArray h = host.toUtf8();
    const QByteArray o = originHost.toUtf8();
    if (ssh_channel_open_forward(channel, h.constData(), port, o.constData(), originPort) != SSH_OK) {
        emit notice(tr("Forward to %1:%2 failed: %3").arg(host).arg(port).arg(QString::fromUtf8(ssh_get_error(session))));
        ssh_channel_free(channel);
        return nullptr;
    }
    return channel;
}

void ForwardManager::addBridge(ssh_channel channel, QIODevice *device, const QByteArray &pending)
{
    ChannelBridge *bridge = new ChannelBridge(channel, device, this);
    connect(bridge, &ChannelBridge::wrote, this, &ForwardManager::wake);
    bridges << bridge;
    bridge->writeToChannel(pending);
}

bool ForwardManager::pump()
{
    bool moved = false;
    const auto current = bridges;
    for (const QPointer<ChannelBridge> &bridge : current)
        if (bridge && bridge->pump() > 0)
            moved = true;
    bridges.removeIf([](const QPointer<ChannelBridge> &b) { return b.isNull(); });
    return moved;
}

void ForwardManager::stop()
{
    qDeleteAll(servers);
    servers.clear();
    for (const QPointer<ChannelBridge> &bridge : std::as_const(bridges))
        delete bridge.data();   // frees the channel now, the session goes next
    bridges.clear();
    const auto clients = findChildren<QObject *>(Qt::FindDirectChildrenOnly);
    for (QObject *child : clients)
        delete child;   // pending SOCKS handshakes
    remoteTargets.clear();
    x11Active = agentActive = false;
}

// --- X11

namespace {
struct Display {
    QString socketPath;   // Unix socket, or
    QString host;         // TCP host
    int number = 0;
    int screen = 0;
};
} // namespace

static bool parseDisplay(Display *d)
{
    QString display = qEnvironmentVariable("DISPLAY");
#ifdef Q_OS_WIN
    if (display.isEmpty())
        display = QStringLiteral("localhost:0.0");   // VcXsrv / Xming default
#endif
    const int colon = display.lastIndexOf(':');
    if (colon < 0)
        return false;
    const QString host = display.left(colon);
    const QString rest = display.mid(colon + 1);
    d->number = rest.section('.', 0, 0).toInt();
    d->screen = rest.section('.', 1, 1).toInt();
    if (host.startsWith('/'))
        d->socketPath = display;   // XQuartz launchd socket
    else if (host.isEmpty() || host == QLatin1String("unix"))
        d->socketPath = QStringLiteral("/tmp/.X11-unix/X%1").arg(d->number);
    else
        d->host = host;
    return true;
}

void ForwardManager::setupShellChannel(ssh_channel shell)
{
    if (site.transferX11) {
        Display display;
        if (!parseDisplay(&display)) {
            emit notice(tr("X11 forwarding: DISPLAY is not set"));
        } else {
            // reuse the real cookie (like ssh -Y) so the local X server accepts the clients
            QString cookie;
            QProcess xauth;
            xauth.start(QStringLiteral("xauth"), {QStringLiteral("list"), qEnvironmentVariable("DISPLAY")});
            if (xauth.waitForFinished(2000)) {
                const QStringList lines = QString::fromLocal8Bit(xauth.readAllStandardOutput()).split('\n');
                for (const QString &line : lines) {
                    const QStringList parts = line.simplified().split(' ');
                    if (parts.size() == 3 && parts.at(1) == QLatin1String("MIT-MAGIC-COOKIE-1")) {
                        cookie = parts.at(2);
                        break;
                    }
                }
            }
            if (cookie.isEmpty()) {
                for (int i = 0; i < 16; ++i)
                    cookie += QStringLiteral("%1").arg(QRandomGenerator::global()->bounded(256), 2, 16, QChar('0'));
            }
            if (ssh_channel_request_x11(shell, 0, "MIT-MAGIC-COOKIE-1", cookie.toLatin1().constData(),
                                        display.screen) == SSH_OK) {
                x11Active = true;
            } else {
                emit notice(tr("X11 forwarding refused by the server"));
            }
        }
    }
    if (site.forwardAgent) {
        if (agentPath().isEmpty())
            emit notice(tr("Agent forwarding: no SSH agent is running"));
        else if (ssh_channel_request_auth_agent(shell) == SSH_OK)
            agentActive = true;
        else
            emit notice(tr("Agent forwarding refused by the server"));
    }
}

ssh_channel ForwardManager::openX11Channel()
{
    Display display;
    if (!x11Active || !parseDisplay(&display))
        return nullptr;
    QIODevice *device = nullptr;
    if (!display.socketPath.isEmpty()) {
        QLocalSocket *socket = new QLocalSocket;
        socket->connectToServer(display.socketPath);
        if (!socket->waitForConnected(2000)) {
            emit notice(tr("X11: cannot connect to %1").arg(display.socketPath));
            delete socket;
            return nullptr;
        }
        device = socket;
    } else {
        QTcpSocket *socket = new QTcpSocket;
        socket->connectToHost(display.host, quint16(6000 + display.number));
        device = socket;
    }
    ssh_channel channel = ssh_channel_new(connection->session());
    if (!channel) {
        delete device;
        return nullptr;
    }
    addBridge(channel, device);
    return channel;
}

// --- agent

QString ForwardManager::agentPath() const
{
    if (!site.agentSocket.isEmpty()) {
        QString path = site.agentSocket;
        if (path.startsWith(QLatin1String("~/")))
            path = QDir::homePath() + path.mid(1);
        return path;
    }
    const QString env = qEnvironmentVariable("SSH_AUTH_SOCK");
#ifdef Q_OS_WIN
    if (env.isEmpty())
        return QStringLiteral("\\\\.\\pipe\\openssh-ssh-agent");
#endif
    return env;
}

ssh_channel ForwardManager::openAgentChannel()
{
    const QString path = agentPath();
    if (!agentActive || path.isEmpty())
        return nullptr;
    QLocalSocket *socket = new QLocalSocket;
    socket->connectToServer(path);
    if (!socket->waitForConnected(2000)) {
        emit notice(tr("Agent forwarding: cannot connect to %1").arg(path));
        delete socket;
        return nullptr;
    }
    ssh_channel channel = ssh_channel_new(connection->session());
    if (!channel) {
        delete socket;
        return nullptr;
    }
    addBridge(channel, socket);
    return channel;
}

// --- remote forwards

ssh_channel ForwardManager::openForwardedChannel(int boundPort)
{
    const auto it = remoteTargets.constFind(boundPort);
    if (it == remoteTargets.constEnd())
        return nullptr;
    QTcpSocket *socket = new QTcpSocket;
    socket->connectToHost(it->first, quint16(it->second));   // writes are buffered until connected
    ssh_channel channel = ssh_channel_new(connection->session());
    if (!channel) {
        delete socket;
        return nullptr;
    }
    addBridge(channel, socket);
    return channel;
}

} // namespace qssh

#include "qssh_proxy.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QHostAddress>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  define QSSH_POLL WSAPoll
static int lastSocketError() { return WSAGetLastError(); }
static bool connectInProgress(int e) { return e == WSAEWOULDBLOCK || e == WSAEINPROGRESS; }
#else
#  include <cerrno>
#  include <cstring>
#  include <fcntl.h>
#  include <netdb.h>
#  include <netinet/in.h>
#  include <poll.h>
#  include <sys/socket.h>
#  include <unistd.h>
#  define QSSH_POLL ::poll
static int lastSocketError() { return errno; }
static bool connectInProgress(int e) { return e == EINPROGRESS; }
#endif

namespace qssh {

void closeSocket(socket_t fd)
{
    if (fd == SSH_INVALID_SOCKET)
        return;
#ifdef _WIN32
    closesocket(fd);
#else
    ::close(fd);
#endif
}

void enableTcpKeepAlive(socket_t fd)
{
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, reinterpret_cast<const char *>(&on), sizeof(on));
}

static QString errorText(int code)
{
#ifdef _WIN32
    return QStringLiteral("socket error %1").arg(code);
#else
    return QString::fromLocal8Bit(strerror(code));
#endif
}

static void setBlocking(socket_t fd, bool blocking)
{
#ifdef _WIN32
    u_long mode = blocking ? 0 : 1;
    ioctlsocket(fd, FIONBIO, &mode);
#else
    const int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK));
#endif
}

// Waits until @p fd is readable/writable; false on timeout
static bool waitFor(socket_t fd, short events, int timeoutMs)
{
    pollfd p;
    p.fd = fd;
    p.events = events;
    p.revents = 0;
    return QSSH_POLL(&p, 1, timeoutMs) > 0;
}

static int familyOf(const SiteInfo &site)
{
    switch (site.addressFamily) {
    case SiteInfo::IPv4: return AF_INET;
    case SiteInfo::IPv6: return AF_INET6;
    default: return AF_UNSPEC;
    }
}

static socket_t connectDirect(const SiteInfo &site, const QString &host, int port,
                              int timeoutMs, QString *error)
{
    addrinfo hints {};
    hints.ai_family = familyOf(site);
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *result = nullptr;
    const QByteArray service = QByteArray::number(port);
    const int rc = getaddrinfo(host.toUtf8().constData(), service.constData(), &hints, &result);
    if (rc != 0) {
        *error = QStringLiteral("Cannot resolve %1: %2").arg(host, QString::fromLocal8Bit(gai_strerror(rc)));
        return SSH_INVALID_SOCKET;
    }

    socket_t fd = SSH_INVALID_SOCKET;
    for (addrinfo *ai = result; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd == SSH_INVALID_SOCKET) {
            *error = errorText(lastSocketError());
            continue;
        }

        if (!site.bindAddress.isEmpty()) {
            addrinfo bhints {};
            bhints.ai_family = ai->ai_family;
            bhints.ai_socktype = SOCK_STREAM;
            bhints.ai_flags = AI_PASSIVE;
            addrinfo *bind_ai = nullptr;
            if (getaddrinfo(site.bindAddress.toUtf8().constData(), nullptr, &bhints, &bind_ai) != 0
                || bind(fd, bind_ai->ai_addr, static_cast<int>(bind_ai->ai_addrlen)) != 0) {
                *error = QStringLiteral("Cannot bind to %1").arg(site.bindAddress);
                if (bind_ai)
                    freeaddrinfo(bind_ai);
                closeSocket(fd);
                fd = SSH_INVALID_SOCKET;
                continue;
            }
            freeaddrinfo(bind_ai);
        }

        setBlocking(fd, false);
        bool ok = ::connect(fd, ai->ai_addr, static_cast<int>(ai->ai_addrlen)) == 0;
        if (!ok && connectInProgress(lastSocketError()) && waitFor(fd, POLLOUT, timeoutMs)) {
            int soError = 0;
            socklen_t len = sizeof(soError);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&soError), &len);
            ok = soError == 0;
            if (!ok)
                *error = errorText(soError);
        } else if (!ok) {
            const int e = lastSocketError();
            *error = connectInProgress(e) ? QStringLiteral("Connection timed out") : errorText(e);
        }
        if (ok) {
            setBlocking(fd, true);
            break;
        }
        closeSocket(fd);
        fd = SSH_INVALID_SOCKET;
    }
    freeaddrinfo(result);
    if (fd == SSH_INVALID_SOCKET)
        *error = QStringLiteral("Cannot connect to %1:%2: %3").arg(host).arg(port).arg(*error);
    return fd;
}

namespace {

// Blocking I/O with an overall deadline, for the proxy handshake
struct Io {
    socket_t fd;
    QElapsedTimer clock;
    int timeoutMs;
    QString *error;

    int remaining() const { return qMax(1, timeoutMs - int(clock.elapsed())); }

    bool send(const QByteArray &data)
    {
        qsizetype done = 0;
        while (done < data.size()) {
            if (!waitFor(fd, POLLOUT, remaining())) {
                *error = QStringLiteral("Proxy timed out");
                return false;
            }
            const auto n = ::send(fd, data.constData() + done, int(data.size() - done), 0);
            if (n <= 0) {
                *error = QStringLiteral("Proxy closed the connection");
                return false;
            }
            done += n;
        }
        return true;
    }

    bool recv(char *out, int size)
    {
        int done = 0;
        while (done < size) {
            if (!waitFor(fd, POLLIN, remaining())) {
                *error = QStringLiteral("Proxy timed out");
                return false;
            }
            const auto n = ::recv(fd, out + done, size - done, 0);
            if (n <= 0) {
                *error = QStringLiteral("Proxy closed the connection");
                return false;
            }
            done += int(n);
        }
        return true;
    }
};

} // namespace

static QByteArray portBytes(int port)
{
    QByteArray b;
    b.append(char((port >> 8) & 0xff));
    b.append(char(port & 0xff));
    return b;
}

static bool socks5(Io &io, const SiteInfo &site, const QString &host, int port)
{
    const bool withAuth = !site.proxyUser.isEmpty();
    QByteArray hello("\x05", 1);
    hello += withAuth ? QByteArray("\x02\x00\x02", 3) : QByteArray("\x01\x00", 2);
    char reply[4];
    if (!io.send(hello) || !io.recv(reply, 2))
        return false;
    if (reply[0] != 5 || quint8(reply[1]) == 0xff) {
        *io.error = QStringLiteral("SOCKS5 proxy accepts none of our authentication methods");
        return false;
    }
    if (reply[1] == 2) {
        const QByteArray user = site.proxyUser.toUtf8().left(255);
        const QByteArray pass = site.proxyPassword.toUtf8().left(255);
        QByteArray auth("\x01", 1);
        auth += char(user.size()) + user + char(pass.size()) + pass;
        if (!io.send(auth) || !io.recv(reply, 2))
            return false;
        if (reply[1] != 0) {
            *io.error = QStringLiteral("SOCKS5 proxy rejected the username or password");
            return false;
        }
    }

    QByteArray request("\x05\x01\x00", 3);
    QHostAddress address;
    if (address.setAddress(host) && address.protocol() == QAbstractSocket::IPv4Protocol) {
        const quint32 v4 = address.toIPv4Address();
        request += '\x01';
        request += char(v4 >> 24) + QByteArray(1, char(v4 >> 16)) + char(v4 >> 8) + char(v4);
    } else if (address.protocol() == QAbstractSocket::IPv6Protocol) {
        const Q_IPV6ADDR v6 = address.toIPv6Address();
        request += '\x04';
        request += QByteArray(reinterpret_cast<const char *>(v6.c), 16);
    } else {
        const QByteArray name = host.toUtf8().left(255);
        request += '\x03';
        request += char(name.size()) + name;
    }
    request += portBytes(port);
    if (!io.send(request) || !io.recv(reply, 4))
        return false;
    if (reply[1] != 0) {
        static const char *const reasons[] = {
            "succeeded", "general failure", "connection not allowed by ruleset",
            "network unreachable", "host unreachable", "connection refused",
            "TTL expired", "command not supported", "address type not supported"};
        const int code = quint8(reply[1]);
        *io.error = QStringLiteral("SOCKS5 proxy: %1")
                        .arg(code < 9 ? QString::fromLatin1(reasons[code]) : QStringLiteral("error %1").arg(code));
        return false;
    }
    // skip the bound address
    int skip = 0;
    switch (reply[3]) {
    case 1: skip = 4; break;
    case 4: skip = 16; break;
    case 3: {
        char len;
        if (!io.recv(&len, 1))
            return false;
        skip = quint8(len);
        break;
    }
    default:
        *io.error = QStringLiteral("SOCKS5 proxy sent an invalid reply");
        return false;
    }
    QByteArray rest(skip + 2, '\0');
    return io.recv(rest.data(), int(rest.size()));
}

static bool socks4(Io &io, const SiteInfo &site, const QString &host, int port)
{
    QByteArray request("\x04\x01", 2);
    request += portBytes(port);
    QHostAddress address;
    const bool literal = address.setAddress(host) && address.protocol() == QAbstractSocket::IPv4Protocol;
    if (literal) {
        const quint32 v4 = address.toIPv4Address();
        request += char(v4 >> 24) + QByteArray(1, char(v4 >> 16)) + char(v4 >> 8) + char(v4);
    } else {
        request += QByteArray("\x00\x00\x00\x01", 4);   // SOCKS4a: let the proxy resolve
    }
    request += site.proxyUser.toUtf8() + '\0';
    if (!literal)
        request += host.toUtf8() + '\0';
    char reply[8];
    if (!io.send(request) || !io.recv(reply, 8))
        return false;
    if (reply[1] != 0x5a) {
        *io.error = QStringLiteral("SOCKS4 proxy refused the connection (code %1)").arg(quint8(reply[1]));
        return false;
    }
    return true;
}

static bool httpConnect(Io &io, const SiteInfo &site, const QString &host, int port)
{
    const QString target = (host.contains(':') ? QStringLiteral("[%1]").arg(host) : host)
                           + QStringLiteral(":%1").arg(port);
    QByteArray request = "CONNECT " + target.toUtf8() + " HTTP/1.1\r\nHost: " + target.toUtf8() + "\r\n";
    if (!site.proxyUser.isEmpty())
        request += "Proxy-Authorization: Basic "
                   + (site.proxyUser + ':' + site.proxyPassword).toUtf8().toBase64() + "\r\n";
    request += "\r\n";
    if (!io.send(request))
        return false;

    // read the response header byte by byte so nothing of the SSH stream is consumed
    QByteArray header;
    char c;
    while (!header.endsWith("\r\n\r\n")) {
        if (header.size() > 16384) {
            *io.error = QStringLiteral("HTTP proxy sent an oversized response");
            return false;
        }
        if (!io.recv(&c, 1))
            return false;
        header += c;
    }
    const QByteArray status = header.left(header.indexOf("\r\n"));
    const QList<QByteArray> parts = status.split(' ');
    if (parts.size() < 2 || parts.at(1) != "200") {
        *io.error = QStringLiteral("HTTP proxy: %1").arg(QString::fromLatin1(status));
        return false;
    }
    return true;
}

socket_t openTcp(const SiteInfo &site, const QString &host, int port, int timeoutSec, QString *error)
{
    const int timeoutMs = (timeoutSec > 0 ? timeoutSec : 30) * 1000;
    const bool proxied = site.proxyType == SiteInfo::Socks5 || site.proxyType == SiteInfo::Socks4
                         || site.proxyType == SiteInfo::Http;
    if (!proxied)
        return connectDirect(site, host, port, timeoutMs, error);

    if (site.proxyHost.isEmpty() || site.proxyPort <= 0) {
        *error = QStringLiteral("The proxy host and port are not set");
        return SSH_INVALID_SOCKET;
    }
    socket_t fd = connectDirect(site, site.proxyHost, site.proxyPort, timeoutMs, error);
    if (fd == SSH_INVALID_SOCKET)
        return fd;

    Io io { fd, {}, timeoutMs, error };
    io.clock.start();
    bool ok = false;
    switch (site.proxyType) {
    case SiteInfo::Socks5: ok = socks5(io, site, host, port); break;
    case SiteInfo::Socks4: ok = socks4(io, site, host, port); break;
    default: ok = httpConnect(io, site, host, port); break;
    }
    if (!ok) {
        closeSocket(fd);
        return SSH_INVALID_SOCKET;
    }
    return fd;
}

} // namespace qssh

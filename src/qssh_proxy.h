#ifndef QSSH_PROXY_H
#define QSSH_PROXY_H

#include <libssh/libssh.h>
#include <QString>

#include "qsshterm_common.h"

namespace qssh {

/**
 * Opens a blocking TCP connection to @p host:@p port, directly or through the
 * SOCKS4/4a, SOCKS5 or HTTP CONNECT proxy of @p site, honouring its address
 * family and bind address. Only the proxy handshake is read from the socket,
 * so it can be handed to libssh (SSH_OPTIONS_FD) right away.
 * Returns SSH_INVALID_SOCKET and sets @p error on failure.
 */
socket_t openTcp(const SiteInfo &site, const QString &host, int port, int timeoutSec, QString *error);

/** Closes a socket returned by openTcp(). */
void closeSocket(socket_t fd);

/** Enables TCP keepalive probes on @p fd. */
void enableTcpKeepAlive(socket_t fd);

} // namespace qssh

#endif

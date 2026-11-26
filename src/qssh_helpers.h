#ifndef QSSH_HELPERS_H
#define QSSH_HELPERS_H

#include <libssh/libssh.h>
#include <libssh/callbacks.h>
#include <QHash>
#include <QString>
#include <functional>
#include <memory>
#include <vector>

#include "qsshterm_common.h"

namespace qssh {

/**
 * One SSH connection built from a SiteInfo: options, ~/.ssh/config, proxy or
 * jump hosts, host key check and authentication (asking the user for
 * passwords, passphrases and one-time codes when needed).
 *
 * Used from a worker thread; prompts are shown on the GUI thread. The object
 * can be opened again after close(); typed passwords and passphrases are kept
 * in memory so a reconnect does not ask again.
 */
class Connection
{
public:
    explicit Connection(const SiteInfo &info);
    ~Connection();
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;

    /** Connects, verifies the host key and authenticates. */
    bool open(QString *error);
    /** Disconnects and frees the session; callable when not open. */
    void close();

    ssh_session session() const { return sess; }
    const SiteInfo &site() const { return info; }

    /** Ask to save a password typed at the prompt (terminal sessions only). */
    bool offerSavePassword = false;
    /** Disable Nagle (interactive terminals). */
    bool noDelay = false;

    /** Set by open() when the user typed the password of the site. */
    bool passwordTyped = false;
    bool savePasswordRequested = false;

    /** Lines worth showing the user (host key added, banner, ...). */
    std::function<void(const QString &)> notice;

    // Channels the server opens, set before open(); return nullptr to refuse.
    std::function<ssh_channel(const char *originAddress, int originPort)> onX11;
    std::function<ssh_channel()> onAgent;
    std::function<ssh_channel(const char *boundAddress, int boundPort,
                              const char *originAddress, int originPort)> onForwardedTcpip;

    struct AuthTarget;
    struct Hop;

private:
    bool applyOptions(QString *error);
    bool verifyHost(ssh_session s, const QString &label, QString *error);
    int authenticate(ssh_session s, AuthTarget &target, QString *error);
    int tryKeyFile(ssh_session s, AuthTarget &target);
    int tryPassword(ssh_session s, AuthTarget &target);
    int tryKbdint(ssh_session s, AuthTarget &target);
    void say(const QString &text);

    static int authCallback(const char *prompt, char *buf, size_t len, int echo, int verify, void *userdata);
    static ssh_channel x11Callback(ssh_session, const char *addr, int port, void *userdata);
    static ssh_channel agentCallback(ssh_session, void *userdata);
    static ssh_channel forwardedCallback(ssh_session, const char *dAddr, int dPort,
                                         const char *oAddr, int oPort, void *userdata);
    static int jumpBeforeConnection(ssh_session s, void *userdata);
    static int jumpVerify(ssh_session s, void *userdata);
    static int jumpAuthenticate(ssh_session s, void *userdata);

    SiteInfo info;
    ssh_session sess = nullptr;
    socket_t ownSocket = SSH_INVALID_SOCKET;
    struct ssh_callbacks_struct callbacks;
    std::vector<std::unique_ptr<Hop>> hops;
    QHash<QString, QString> passphrases;   // key file -> passphrase typed this run
    QString jumpError;
};

} // namespace qssh

#endif

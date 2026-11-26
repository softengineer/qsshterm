#include "qssh_helpers.h"
#include "qssh_prompt.h"
#include "qssh_proxy.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QObject>
#include <QStringList>
#include <cstring>

namespace qssh {

struct Connection::AuthTarget {
    QString label;            // user@host, for prompts
    QString password;         // saved one on input, typed one on output
    QString keyPath;
    QString certPath;
    bool publicKey = true;
    bool agent = true;
    bool passwordAuth = true;
    bool kbdint = true;
    bool gssapi = true;
    bool offerSave = false;
    bool typed = false;       // password came from the user
    bool save = false;
    bool cancelled = false;
};

struct Connection::Hop {
    Connection *owner = nullptr;
    int index = 0;
    struct ssh_jump_callbacks_struct jumpCallbacks;
    struct ssh_callbacks_struct sessionCallbacks;
};

namespace {
// state of one private key import, for the passphrase callback
struct KeyPrompt {
    QString path;
    bool asked = false;
    bool cancelled = false;
    QString typed;
};
} // namespace

static QString expandHome(const QString &path)
{
    if (path == QLatin1String("~"))
        return QDir::homePath();
    if (path.startsWith(QLatin1String("~/")))
        return QDir::homePath() + path.mid(1);
    return path;
}

static int copyAnswer(const QString &text, char *buf, size_t len)
{
    const QByteArray bytes = text.toUtf8();
    if (len == 0 || size_t(bytes.size()) >= len)
        return -1;
    memcpy(buf, bytes.constData(), size_t(bytes.size()) + 1);
    return 0;
}

static int keyPassphraseCallback(const char *, char *buf, size_t len, int, int, void *userdata)
{
    KeyPrompt *kp = static_cast<KeyPrompt *>(userdata);
    kp->asked = true;
    const PromptResult r = askText(QObject::tr("Key Passphrase"),
                                   QObject::tr("Enter the passphrase for %1").arg(kp->path),
                                   QObject::tr("Passphrase"), false);
    if (!r.ok) {
        kp->cancelled = true;
        return -1;
    }
    kp->typed = r.text;
    return copyAnswer(r.text, buf, len);
}

Connection::Connection(const SiteInfo &site)
    : info(site)
{
    memset(&callbacks, 0, sizeof(callbacks));
}

Connection::~Connection()
{
    close();
}

void Connection::say(const QString &text)
{
    if (notice)
        notice(text);
}

void Connection::close()
{
    if (sess) {
        ssh_disconnect(sess);
        ssh_free(sess);
        sess = nullptr;
    }
    closeSocket(ownSocket);   // libssh never closes a socket passed with SSH_OPTIONS_FD
    ownSocket = SSH_INVALID_SOCKET;
    hops.clear();
}

bool Connection::applyOptions(QString *error)
{
    auto set = [&](ssh_options_e option, const void *value, const QString &what) {
        if (ssh_options_set(sess, option, value) < 0) {
            *error = QObject::tr("Invalid %1: %2").arg(what, QString::fromUtf8(ssh_get_error(sess)));
            return false;
        }
        return true;
    };
    auto setText = [&](ssh_options_e option, const QString &value, const QString &what) {
        if (value.trimmed().isEmpty())
            return true;
        const QByteArray bytes = value.trimmed().toUtf8();
        return set(option, bytes.constData(), what);
    };

    if (!setText(SSH_OPTIONS_HOST, info.hostname, QObject::tr("host")))
        return false;

    // ~/.ssh/config first (Host aliases, HostName, IdentityFile, ProxyJump ...),
    // then the session's own settings on top of it
    if (info.useSshConfig) {
        if (ssh_options_parse_config(sess, nullptr) < 0)
            say(QObject::tr("Warning: cannot parse ~/.ssh/config"));
    } else {
        bool process = false;
        ssh_options_set(sess, SSH_OPTIONS_PROCESS_CONFIG, &process);
    }

    if (!setText(SSH_OPTIONS_USER, info.username, QObject::tr("username")))
        return false;
    // port is -1 (no <host> element) or 0 when unset: keep libssh's / ssh config's
    if (info.port > 0) {
        unsigned int port = unsigned(info.port);
        if (!set(SSH_OPTIONS_PORT, &port, QObject::tr("port")))
            return false;
    }
    if (info.connectTimeout > 0) {
        long timeout = info.connectTimeout;
        set(SSH_OPTIONS_TIMEOUT, &timeout, QObject::tr("timeout"));
    }
    int verbosity = info.logLevel;
    set(SSH_OPTIONS_LOG_VERBOSITY, &verbosity, QObject::tr("log level"));

    if (!info.knownHostsFile.isEmpty()) {
        const QByteArray file = QFile::encodeName(expandHome(info.knownHostsFile));
        if (!set(SSH_OPTIONS_KNOWNHOSTS, file.constData(), QObject::tr("known hosts file")))
            return false;
    }
    if (info.compression >= 0
        && !set(SSH_OPTIONS_COMPRESSION, info.compression ? "yes" : "no", QObject::tr("compression")))
        return false;
    if (!setText(SSH_OPTIONS_CIPHERS_C_S, info.ciphers, QObject::tr("cipher list"))
        || !setText(SSH_OPTIONS_CIPHERS_S_C, info.ciphers, QObject::tr("cipher list"))
        || !setText(SSH_OPTIONS_KEY_EXCHANGE, info.kexAlgorithms, QObject::tr("key exchange list"))
        || !setText(SSH_OPTIONS_HMAC_C_S, info.macs, QObject::tr("MAC list"))
        || !setText(SSH_OPTIONS_HMAC_S_C, info.macs, QObject::tr("MAC list"))
        || !setText(SSH_OPTIONS_HOSTKEYS, info.hostKeyAlgorithms, QObject::tr("host key algorithm list"))
        || !setText(SSH_OPTIONS_PUBLICKEY_ACCEPTED_TYPES, info.pubkeyAcceptedTypes,
                    QObject::tr("public key type list")))
        return false;

    int delegate = info.gssapiDelegate ? 1 : 0;
    ssh_options_set(sess, SSH_OPTIONS_GSSAPI_DELEGATE_CREDENTIALS, &delegate);

    // an agent socket that cannot exist turns the agent off for this session
    if (!info.authAgent)
        set(SSH_OPTIONS_IDENTITY_AGENT, "none", QObject::tr("agent socket"));
    else if (!setText(SSH_OPTIONS_IDENTITY_AGENT, info.agentSocket, QObject::tr("agent socket")))
        return false;

    if (noDelay) {
        int on = 1;
        ssh_options_set(sess, SSH_OPTIONS_NODELAY, &on);
    }
    return true;
}

bool Connection::open(QString *error)
{
    close();
    passwordTyped = false;
    savePasswordRequested = false;
    jumpError.clear();

    sess = ssh_new();
    if (!sess) {
        *error = QObject::tr("Unable to create an SSH session.");
        return false;
    }
    if (!applyOptions(error)) {
        close();
        return false;
    }

    memset(&callbacks, 0, sizeof(callbacks));
    ssh_callbacks_init(&callbacks);
    callbacks.userdata = this;
    callbacks.auth_function = &Connection::authCallback;
    if (onX11)
        callbacks.channel_open_request_x11_function = &Connection::x11Callback;
    if (onAgent)
        callbacks.channel_open_request_auth_agent_function = &Connection::agentCallback;
    if (onForwardedTcpip)
        callbacks.channel_open_request_forwarded_tcpip_function = &Connection::forwardedCallback;
    ssh_set_callbacks(sess, &callbacks);

    const bool proxied = info.proxyType == SiteInfo::Socks5 || info.proxyType == SiteInfo::Socks4
                         || info.proxyType == SiteInfo::Http;

    if (!info.jumpChain.isEmpty()) {
        QStringList spec;
        for (int i = 0; i < info.jumpChain.size(); ++i) {
            const JumpHost &j = info.jumpChain.at(i);
            QString hop = j.username.isEmpty() ? QString() : j.username + '@';
            hop += j.hostname.contains(':') ? QStringLiteral("[%1]").arg(j.hostname) : j.hostname;
            hop += QStringLiteral(":%1").arg(j.port > 0 ? j.port : 22);
            spec << hop;

            auto h = std::make_unique<Hop>();
            h->owner = this;
            h->index = i;
            h->jumpCallbacks.userdata = h.get();
            h->jumpCallbacks.before_connection = &Connection::jumpBeforeConnection;
            h->jumpCallbacks.verify_knownhost = &Connection::jumpVerify;
            h->jumpCallbacks.authenticate = &Connection::jumpAuthenticate;
            hops.push_back(std::move(h));
        }
        const QByteArray jumps = spec.join(',').toUtf8();
        if (ssh_options_set(sess, SSH_OPTIONS_PROXYJUMP, jumps.constData()) < 0) {
            *error = QObject::tr("Invalid jump host list: %1").arg(QString::fromUtf8(ssh_get_error(sess)));
            close();
            return false;
        }
        for (auto &h : hops)
            ssh_options_set(sess, SSH_OPTIONS_PROXYJUMP_CB_LIST_APPEND, &h->jumpCallbacks);
        if (info.proxyType != SiteInfo::NoProxy)
            say(QObject::tr("Note: the proxy setting is not used with jump hosts."));
    } else if (info.proxyType == SiteInfo::ProxyCommand && !info.proxyCommand.trimmed().isEmpty()) {
        const QByteArray command = info.proxyCommand.trimmed().toUtf8();
        if (ssh_options_set(sess, SSH_OPTIONS_PROXYCOMMAND, command.constData()) < 0) {
            *error = QObject::tr("Proxy command is not supported: %1").arg(QString::fromUtf8(ssh_get_error(sess)));
            close();
            return false;
        }
    } else if (proxied || info.addressFamily != SiteInfo::AnyFamily) {
        // open the TCP connection ourselves and hand it to libssh
        char *host = nullptr;
        ssh_options_get(sess, SSH_OPTIONS_HOST, &host);   // HostName from ssh config applied
        const QString target = host ? QString::fromUtf8(host) : info.hostname;
        ssh_string_free_char(host);
        unsigned int port = 22;
        ssh_options_get_port(sess, &port);
        ownSocket = openTcp(info, target, int(port), info.connectTimeout, error);
        if (ownSocket == SSH_INVALID_SOCKET) {
            close();
            return false;
        }
        ssh_options_set(sess, SSH_OPTIONS_FD, &ownSocket);
    } else if (!info.bindAddress.isEmpty()) {
        const QByteArray bind = info.bindAddress.toUtf8();
        ssh_options_set(sess, SSH_OPTIONS_BINDADDR, bind.constData());
    }

    if (ssh_connect(sess) != SSH_OK) {
        *error = jumpError.isEmpty()
                     ? QObject::tr("Cannot connect to %1: %2").arg(info.hostname, QString::fromUtf8(ssh_get_error(sess)))
                     : jumpError;
        close();
        return false;
    }
    if (info.keepAliveInterval > 0)
        enableTcpKeepAlive(ssh_get_fd(sess));

    if (!verifyHost(sess, info.hostname, error)) {
        close();
        return false;
    }

    AuthTarget target;
    char *user = nullptr;
    ssh_options_get(sess, SSH_OPTIONS_USER, &user);
    target.label = QStringLiteral("%1@%2").arg(user ? QString::fromUtf8(user) : QString(), info.hostname);
    ssh_string_free_char(user);
    target.password = info.password;
    target.keyPath = info.privateKeyPath;
    target.certPath = info.certificatePath;
    target.publicKey = info.authPublicKey;
    target.agent = info.authAgent;
    target.passwordAuth = info.authPassword;
    target.kbdint = info.authKeyboardInteractive;
    target.gssapi = info.authGssapi;
    target.offerSave = offerSavePassword;

    if (authenticate(sess, target, error) != SSH_AUTH_SUCCESS) {
        close();
        return false;
    }
    if (target.typed) {
        info.password = target.password;   // reused by reconnects
        passwordTyped = true;
        savePasswordRequested = target.save;
    }

    if (char *banner = ssh_get_issue_banner(sess)) {
        say(QString::fromUtf8(banner).trimmed());
        ssh_string_free_char(banner);
    }
    return true;
}

bool Connection::verifyHost(ssh_session s, const QString &label, QString *error)
{
    if (info.hostKeyCheck == SiteInfo::HostKeyOff)
        return true;

    ssh_key serverKey = nullptr;
    if (ssh_get_server_publickey(s, &serverKey) != SSH_OK) {
        *error = QObject::tr("Unable to get the server public key of %1: %2")
                     .arg(label, QString::fromUtf8(ssh_get_error(s)));
        return false;
    }
    const QString keyType = QString::fromLatin1(ssh_key_type_to_char(ssh_key_type(serverKey))).toUpper();
    unsigned char *hash = nullptr;
    size_t hashLength = 0;
    const int rc = ssh_get_publickey_hash(serverKey, SSH_PUBLICKEY_HASH_SHA256, &hash, &hashLength);
    ssh_key_free(serverKey);
    if (rc != SSH_OK) {
        *error = QObject::tr("Unable to compute the host key fingerprint of %1.").arg(label);
        return false;
    }
    char *fingerprintStr = ssh_get_fingerprint_hash(SSH_PUBLICKEY_HASH_SHA256, hash, hashLength);
    const QString fingerprint = fingerprintStr ? QString::fromLatin1(fingerprintStr) : QString();
    ssh_string_free_char(fingerprintStr);
    ssh_clean_pubkey_hash(&hash);

    switch (ssh_session_is_known_server(s)) {
    case SSH_KNOWN_HOSTS_OK:
        return true;

    case SSH_KNOWN_HOSTS_CHANGED:
        *error = QObject::tr("WARNING: the host key of %1 has changed (now %2 %3).\r\n"
                             "Someone could be eavesdropping on you; refusing to connect.\r\n"
                             "If the change is expected, remove the old entry from the known hosts file.")
                     .arg(label, keyType, fingerprint);
        return false;

    case SSH_KNOWN_HOSTS_OTHER:
        *error = QObject::tr("WARNING: %1 offered a different type of host key (%2 %3) than the one "
                             "in the known hosts file; refusing to connect.")
                     .arg(label, keyType, fingerprint);
        return false;

    case SSH_KNOWN_HOSTS_NOT_FOUND:
    case SSH_KNOWN_HOSTS_UNKNOWN:
        if (info.hostKeyCheck == SiteInfo::HostKeyStrict) {
            *error = QObject::tr("The host key of %1 (%2 %3) is not known and strict host key checking "
                                 "is on; refusing to connect.")
                         .arg(label, keyType, fingerprint);
            return false;
        }
        if (info.hostKeyCheck == SiteInfo::HostKeyAsk
            && !askYesNo(QObject::tr("Unknown Host Key"),
                         QObject::tr("The authenticity of host '%1' can't be established.\n\n"
                                     "%2 key fingerprint is\n%3\n\n"
                                     "Trust this host and remember its key?")
                             .arg(label, keyType, fingerprint))) {
            *error = QObject::tr("Host key of %1 was not accepted.").arg(label);
            return false;
        }
        if (ssh_session_update_known_hosts(s) != SSH_OK)
            say(QObject::tr("Warning: host key %1 could not be saved to known_hosts: %2")
                    .arg(fingerprint, QString::fromUtf8(ssh_get_error(s))));
        else
            say(QObject::tr("Permanently added %1 key %2 of %3 to the list of known hosts.")
                    .arg(keyType, fingerprint, label));
        return true;

    case SSH_KNOWN_HOSTS_ERROR:
    default:
        *error = QObject::tr("Error checking the known hosts file: %1").arg(QString::fromUtf8(ssh_get_error(s)));
        return false;
    }
}

int Connection::tryKeyFile(ssh_session s, AuthTarget &t)
{
    const QString path = expandHome(t.keyPath);
    if (!QFile::exists(path)) {
        say(QObject::tr("Private key %1 not found").arg(path));
        return SSH_AUTH_DENIED;
    }
    const QByteArray file = QFile::encodeName(path);
    ssh_key key = nullptr;
    int rc = SSH_ERROR;
    const QString cached = passphrases.value(path);
    if (!cached.isEmpty())
        rc = ssh_pki_import_privkey_file(file.constData(), cached.toUtf8().constData(), nullptr, nullptr, &key);
    // libssh calls the passphrase callback only for encrypted keys
    for (int attempt = 0; rc != SSH_OK && attempt < 3; ++attempt) {
        KeyPrompt kp;
        kp.path = path;
        rc = ssh_pki_import_privkey_file(file.constData(), nullptr, keyPassphraseCallback, &kp, &key);
        if (kp.cancelled) {
            t.cancelled = true;
            return SSH_AUTH_DENIED;
        }
        if (rc == SSH_OK && kp.asked)
            passphrases.insert(path, kp.typed);
        if (!kp.asked)
            break;   // not a passphrase problem
    }
    if (rc != SSH_OK) {
        say(QObject::tr("Cannot load private key %1").arg(path));
        return SSH_AUTH_DENIED;
    }

    const QString cert = t.certPath.isEmpty() ? path + QStringLiteral("-cert.pub") : expandHome(t.certPath);
    if (QFile::exists(cert)) {
        ssh_key certKey = nullptr;
        if (ssh_pki_import_cert_file(QFile::encodeName(cert).constData(), &certKey) == SSH_OK
            && ssh_pki_copy_cert_to_privkey(certKey, key) == SSH_OK) {
            ssh_key_free(certKey);
        } else {
            ssh_key_free(certKey);
            say(QObject::tr("Cannot use certificate %1").arg(cert));
        }
    } else if (!t.certPath.isEmpty()) {
        say(QObject::tr("Certificate %1 not found").arg(cert));
    }

    rc = ssh_userauth_publickey(s, nullptr, key);
    ssh_key_free(key);
    return rc;
}

int Connection::tryPassword(ssh_session s, AuthTarget &t)
{
    for (int attempt = 0; attempt < 3; ++attempt) {
        QString password = t.password;
        bool typed = false;
        bool save = false;
        if (password.isEmpty() || attempt > 0) {
            const PromptResult r = askText(QObject::tr("Password"),
                                           attempt > 0 ? QObject::tr("Permission denied, please try again.") : QString(),
                                           QObject::tr("Password for %1").arg(t.label), false, t.offerSave);
            if (!r.ok) {
                t.cancelled = true;
                return SSH_AUTH_DENIED;
            }
            password = r.text;
            typed = true;
            save = r.save;
        }
        const int rc = ssh_userauth_password(s, nullptr, password.toUtf8().constData());
        if (rc == SSH_AUTH_SUCCESS || rc == SSH_AUTH_PARTIAL) {
            if (typed) {
                t.password = password;
                t.typed = true;
                t.save = save;
            }
            return rc;
        }
        if (rc == SSH_AUTH_ERROR)
            return rc;
    }
    return SSH_AUTH_DENIED;
}

int Connection::tryKbdint(ssh_session s, AuthTarget &t)
{
    bool usedSaved = false;
    int rc = ssh_userauth_kbdint(s, nullptr, nullptr);
    for (int round = 0; rc == SSH_AUTH_INFO && round < 20; ++round) {
        const QString name = QString::fromUtf8(ssh_userauth_kbdint_getname(s)).trimmed();
        const QString instruction = QString::fromUtf8(ssh_userauth_kbdint_getinstruction(s)).trimmed();
        const int prompts = ssh_userauth_kbdint_getnprompts(s);
        for (int i = 0; i < prompts; ++i) {
            char echo = 0;
            const char *p = ssh_userauth_kbdint_getprompt(s, i, &echo);
            if (!p)
                return SSH_AUTH_ERROR;
            const QString prompt = QString::fromUtf8(p);
            const bool isPassword = !echo && prompt.contains(QLatin1String("password"), Qt::CaseInsensitive);
            QString answer;
            if (isPassword && !usedSaved && !t.password.isEmpty()) {
                answer = t.password;
                usedSaved = true;
            } else {
                QStringList text;
                if (!name.isEmpty())
                    text << name;
                if (!instruction.isEmpty())
                    text << instruction;
                const PromptResult r = askText(QObject::tr("Authentication for %1").arg(t.label),
                                               text.join('\n'), prompt, echo,
                                               isPassword && t.offerSave);
                if (!r.ok) {
                    t.cancelled = true;
                    return SSH_AUTH_DENIED;
                }
                answer = r.text;
                if (isPassword) {
                    t.password = answer;
                    t.typed = true;
                    t.save = r.save;
                    usedSaved = true;
                }
            }
            if (ssh_userauth_kbdint_setanswer(s, unsigned(i), answer.toUtf8().constData()) < 0)
                return SSH_AUTH_ERROR;
        }
        rc = ssh_userauth_kbdint(s, nullptr, nullptr);
    }
    return rc;
}

int Connection::authenticate(ssh_session s, AuthTarget &t, QString *error)
{
    int rc = ssh_userauth_none(s, nullptr);
    if (rc == SSH_AUTH_SUCCESS)
        return rc;

    bool triedGssapi = false, triedKey = false, triedAuto = false, triedPassword = false, triedKbdint = false;
    for (int guard = 0; rc != SSH_AUTH_ERROR && guard < 20; ++guard) {
        const int methods = ssh_userauth_list(s, nullptr);
        if (t.gssapi && !triedGssapi && (methods & SSH_AUTH_METHOD_GSSAPI_MIC)) {
            triedGssapi = true;
            rc = ssh_userauth_gssapi(s);
        } else if (t.publicKey && !triedKey && !t.keyPath.isEmpty() && (methods & SSH_AUTH_METHOD_PUBLICKEY)) {
            triedKey = true;
            rc = tryKeyFile(s, t);
        } else if ((t.publicKey || t.agent) && !triedAuto && (methods & SSH_AUTH_METHOD_PUBLICKEY)) {
            triedAuto = true;
            // agent, then IdentityFile entries and the default ~/.ssh keys
            rc = t.publicKey ? ssh_userauth_publickey_auto(s, nullptr, nullptr)
                             : ssh_userauth_agent(s, nullptr);
        } else if (t.passwordAuth && !triedPassword && (methods & SSH_AUTH_METHOD_PASSWORD)) {
            triedPassword = true;
            rc = tryPassword(s, t);
        } else if (t.kbdint && !triedKbdint && (methods & SSH_AUTH_METHOD_INTERACTIVE)) {
            triedKbdint = true;
            rc = tryKbdint(s, t);
        } else {
            break;   // nothing left to try
        }

        if (rc == SSH_AUTH_SUCCESS)
            return rc;
        if (t.cancelled) {
            *error = QObject::tr("Authentication to %1 cancelled.").arg(t.label);
            return SSH_AUTH_DENIED;
        }
        if (rc == SSH_AUTH_PARTIAL)
            triedKbdint = false;   // e.g. publickey followed by an OTP prompt
    }

    if (error->isEmpty()) {
        const QString detail = QString::fromUtf8(ssh_get_error(s));
        *error = QObject::tr("Authentication to %1 failed%2").arg(t.label, detail.isEmpty() ? QString() : ": " + detail);
    }
    return rc == SSH_AUTH_ERROR ? SSH_AUTH_ERROR : SSH_AUTH_DENIED;
}

// ---------------------------------------------------------------------------
// libssh callbacks

int Connection::authCallback(const char *prompt, char *buf, size_t len, int echo, int, void *)
{
    const PromptResult r = askText(QObject::tr("Key Passphrase"), QString(),
                                   QString::fromUtf8(prompt ? prompt : "Passphrase"), echo != 0);
    return r.ok ? copyAnswer(r.text, buf, len) : -1;
}

ssh_channel Connection::x11Callback(ssh_session, const char *addr, int port, void *userdata)
{
    Connection *c = static_cast<Connection *>(userdata);
    return c->onX11 ? c->onX11(addr, port) : nullptr;
}

ssh_channel Connection::agentCallback(ssh_session, void *userdata)
{
    Connection *c = static_cast<Connection *>(userdata);
    return c->onAgent ? c->onAgent() : nullptr;
}

ssh_channel Connection::forwardedCallback(ssh_session, const char *dAddr, int dPort,
                                          const char *oAddr, int oPort, void *userdata)
{
    Connection *c = static_cast<Connection *>(userdata);
    return c->onForwardedTcpip ? c->onForwardedTcpip(dAddr, dPort, oAddr, oPort) : nullptr;
}

// Jump host callbacks run on libssh's proxy thread while ssh_connect() waits.

int Connection::jumpBeforeConnection(ssh_session s, void *userdata)
{
    Hop *hop = static_cast<Hop *>(userdata);
    Connection *c = hop->owner;
    const SiteInfo &info = c->info;
    if (info.connectTimeout > 0) {
        long timeout = info.connectTimeout;
        ssh_options_set(s, SSH_OPTIONS_TIMEOUT, &timeout);
    }
    int verbosity = info.logLevel;
    ssh_options_set(s, SSH_OPTIONS_LOG_VERBOSITY, &verbosity);
    if (!info.knownHostsFile.isEmpty()) {
        const QByteArray file = QFile::encodeName(expandHome(info.knownHostsFile));
        ssh_options_set(s, SSH_OPTIONS_KNOWNHOSTS, file.constData());
    }
    if (!info.authAgent) {
        ssh_options_set(s, SSH_OPTIONS_IDENTITY_AGENT, "none");
    } else if (!info.agentSocket.isEmpty()) {
        const QByteArray agent = info.agentSocket.toUtf8();
        ssh_options_set(s, SSH_OPTIONS_IDENTITY_AGENT, agent.constData());
    }
    memset(&hop->sessionCallbacks, 0, sizeof(hop->sessionCallbacks));
    ssh_callbacks_init(&hop->sessionCallbacks);
    hop->sessionCallbacks.userdata = c;
    hop->sessionCallbacks.auth_function = &Connection::authCallback;
    ssh_set_callbacks(s, &hop->sessionCallbacks);
    return 0;
}

int Connection::jumpVerify(ssh_session s, void *userdata)
{
    Hop *hop = static_cast<Hop *>(userdata);
    Connection *c = hop->owner;
    QString error;
    if (!c->verifyHost(s, c->info.jumpChain.at(hop->index).label, &error)) {
        c->jumpError = error;
        return -1;
    }
    return 0;
}

int Connection::jumpAuthenticate(ssh_session s, void *userdata)
{
    Hop *hop = static_cast<Hop *>(userdata);
    Connection *c = hop->owner;
    JumpHost &jump = c->info.jumpChain[hop->index];

    AuthTarget target;
    char *user = nullptr;
    ssh_options_get(s, SSH_OPTIONS_USER, &user);
    target.label = QStringLiteral("%1@%2").arg(user ? QString::fromUtf8(user) : QString(), jump.hostname);
    ssh_string_free_char(user);
    target.password = jump.password;
    target.keyPath = jump.privateKeyPath;
    target.certPath = jump.certificatePath;
    target.agent = c->info.authAgent;

    QString error;
    if (c->authenticate(s, target, &error) != SSH_AUTH_SUCCESS) {
        c->jumpError = QObject::tr("Jump host %1: %2").arg(jump.label, error);
        return -1;
    }
    if (target.typed)
        jump.password = target.password;   // reused by reconnects
    return 0;
}

} // namespace qssh

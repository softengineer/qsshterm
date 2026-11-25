#include "qssh_helpers.h"

#include <QByteArray>
#include <QFile>

namespace qssh {

void applySiteOptions(ssh_session session, const SiteInfo &info)
{
    if (!info.username.isEmpty())
        ssh_options_set(session, SSH_OPTIONS_USER, info.username.toUtf8().constData());

    ssh_options_set(session, SSH_OPTIONS_HOST, info.hostname.toUtf8().constData());

    // SiteInfo::port is -1 (no <host> element) or 0 (no port attribute) when unset;
    // leave libssh's default (22) in that case.
    if (info.port > 0) {
        unsigned int port = static_cast<unsigned int>(info.port);
        ssh_options_set(session, SSH_OPTIONS_PORT, &port);
    }

    // Pick up ~/.ssh/config (Host aliases, HostName, User, Port, IdentityFile,
    // ProxyJump, ...) like the ssh command line client. Values set explicitly
    // above take precedence; a missing config file is not an error.
    ssh_options_parse_config(session, nullptr);
}

bool verifyKnownHost(ssh_session session, QString *message)
{
    ssh_key serverKey = nullptr;
    if (ssh_get_server_publickey(session, &serverKey) != SSH_OK) {
        *message = QStringLiteral("Unable to get the server public key: %1")
                       .arg(QString::fromUtf8(ssh_get_error(session)));
        return false;
    }

    unsigned char *hash = nullptr;
    size_t hashLength = 0;
    const int rc = ssh_get_publickey_hash(serverKey, SSH_PUBLICKEY_HASH_SHA256, &hash, &hashLength);
    ssh_key_free(serverKey);
    if (rc != SSH_OK) {
        *message = QStringLiteral("Unable to compute the server key fingerprint.");
        return false;
    }

    char *fingerprintStr = ssh_get_fingerprint_hash(SSH_PUBLICKEY_HASH_SHA256, hash, hashLength);
    const QString fingerprint = fingerprintStr ? QString::fromLatin1(fingerprintStr) : QString();
    ssh_string_free_char(fingerprintStr);
    ssh_clean_pubkey_hash(&hash);

    switch (ssh_session_is_known_server(session)) {
    case SSH_KNOWN_HOSTS_OK:
        return true;

    case SSH_KNOWN_HOSTS_CHANGED:
        *message = QStringLiteral("WARNING: the host key of this server has changed (now %1).\r\n"
                                  "Someone could be eavesdropping on you; refusing to connect.\r\n"
                                  "If the change is expected, remove the old entry from ~/.ssh/known_hosts.")
                       .arg(fingerprint);
        return false;

    case SSH_KNOWN_HOSTS_OTHER:
        *message = QStringLiteral("WARNING: the server offered a different type of host key than the one "
                                  "recorded in ~/.ssh/known_hosts (%1); refusing to connect.")
                       .arg(fingerprint);
        return false;

    case SSH_KNOWN_HOSTS_NOT_FOUND:
    case SSH_KNOWN_HOSTS_UNKNOWN:
        if (ssh_session_update_known_hosts(session) != SSH_OK) {
            *message = QStringLiteral("Warning: host key %1 could not be saved to known_hosts: %2")
                           .arg(fingerprint, QString::fromUtf8(ssh_get_error(session)));
        } else {
            *message = QStringLiteral("Permanently added host key %1 to the list of known hosts.")
                           .arg(fingerprint);
        }
        return true;

    case SSH_KNOWN_HOSTS_ERROR:
    default:
        *message = QStringLiteral("Host key verification failed: %1")
                       .arg(QString::fromUtf8(ssh_get_error(session)));
        return false;
    }
}

static int authenticateKbdint(ssh_session session, const QByteArray &password)
{
    int rc = ssh_userauth_kbdint(session, nullptr, nullptr);
    while (rc == SSH_AUTH_INFO) {
        const int prompts = ssh_userauth_kbdint_getnprompts(session);
        for (int i = 0; i < prompts; ++i) {
            char echo = 0;
            const char *prompt = ssh_userauth_kbdint_getprompt(session, i, &echo);
            if (!prompt)
                break;
            // There is no interactive prompt UI (the old code blocked on the
            // process' stdin); only hidden prompts can be answered, with the
            // password from the site configuration.
            if (echo || password.isEmpty())
                return SSH_AUTH_DENIED;
            if (ssh_userauth_kbdint_setanswer(session, i, password.constData()) < 0)
                return SSH_AUTH_ERROR;
        }
        rc = ssh_userauth_kbdint(session, nullptr, nullptr);
    }
    return rc;
}

static bool isFinal(int rc)
{
    return rc == SSH_AUTH_SUCCESS || rc == SSH_AUTH_ERROR;
}

int authenticate(ssh_session session, const SiteInfo &info)
{
    int rc = ssh_userauth_none(session, nullptr);
    if (isFinal(rc))
        return rc;

    const int methods = ssh_userauth_list(session, nullptr);

    if (methods & SSH_AUTH_METHOD_GSSAPI_MIC) {
        rc = ssh_userauth_gssapi(session);
        if (isFinal(rc))
            return rc;
    }

    if (!info.privateKeyPath.isEmpty() && (methods & SSH_AUTH_METHOD_PUBLICKEY)) {
        ssh_key privateKey = nullptr;
        if (ssh_pki_import_privkey_file(QFile::encodeName(info.privateKeyPath).constData(),
                                        nullptr, nullptr, nullptr, &privateKey) == SSH_OK) {
            rc = ssh_userauth_publickey(session, nullptr, privateKey);
            ssh_key_free(privateKey);
            if (isFinal(rc))
                return rc;
        }
    }

    // ssh-agent, then IdentityFile entries from ~/.ssh/config and the
    // default keys (~/.ssh/id_ed25519, id_ecdsa, id_rsa)
    if (methods & SSH_AUTH_METHOD_PUBLICKEY) {
        rc = ssh_userauth_publickey_auto(session, nullptr, nullptr);
        if (isFinal(rc))
            return rc;
    }

    const QByteArray password = info.password.toUtf8();

    if (!password.isEmpty() && (methods & SSH_AUTH_METHOD_PASSWORD)) {
        rc = ssh_userauth_password(session, nullptr, password.constData());
        if (isFinal(rc))
            return rc;
    }

    if (methods & SSH_AUTH_METHOD_INTERACTIVE) {
        rc = authenticateKbdint(session, password);
        if (isFinal(rc))
            return rc;
    }

    return SSH_AUTH_DENIED;
}

} // namespace qssh

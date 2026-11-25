#ifndef QSSH_HELPERS_H
#define QSSH_HELPERS_H

#include <libssh/libssh.h>
#include <QString>

#include "qsshterm_common.h"

namespace qssh {

/**
 * Applies user, host and port of the site to a freshly created session, then
 * reads ~/.ssh/config for anything the site does not set.
 */
void applySiteOptions(ssh_session session, const SiteInfo &info);

/**
 * Checks the server host key against ~/.ssh/known_hosts.
 * Unknown hosts are trusted on first use and recorded; changed keys are refused.
 * @p message receives text worth showing to the user (may be set on success too).
 */
bool verifyKnownHost(ssh_session session, QString *message);

/**
 * Tries GSSAPI, the configured private key, ssh-agent and the default
 * ~/.ssh keys, the configured password and
 * keyboard-interactive (answered with the configured password) once each.
 * Returns an SSH_AUTH_* code.
 */
int authenticate(ssh_session session, const SiteInfo &info);

} // namespace qssh

#endif

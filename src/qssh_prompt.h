#ifndef QSSH_PROMPT_H
#define QSSH_PROMPT_H

#include <QString>

namespace qssh {

struct PromptResult {
    bool ok = false;     // false: the user cancelled
    QString text;
    bool save = false;   // "Save password" was ticked
};

/**
 * Asks the user for a password, passphrase or one-time code. Callable from
 * any thread: worker threads block until the GUI thread has an answer.
 * @p echo shows the typed text (e.g. a username or OTP prompt that echoes),
 * @p offerSave adds a "Save password" check box.
 */
PromptResult askText(const QString &title, const QString &instruction,
                     const QString &label, bool echo, bool offerSave = false);

/** Yes/No question (e.g. trust an unknown host key); callable from any thread. */
bool askYesNo(const QString &title, const QString &text);

} // namespace qssh

#endif

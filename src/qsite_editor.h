#ifndef QSITE_EDITOR_H
#define QSITE_EDITOR_H

#include <QStringList>

#include "qsshterm_common.h"

class QWidget;

namespace qssh {

/**
 * Tabbed editor for all settings of one session (Connection, Authentication,
 * Forwarding, Terminal, Advanced). @p sessionNames feeds the jump host list.
 * Returns false when cancelled; @p info is only changed on OK.
 */
bool editSite(QWidget *parent, const QString &title, SiteInfo &info, const QStringList &sessionNames);

} // namespace qssh

#endif

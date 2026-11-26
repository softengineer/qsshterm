#include "qssh_prompt.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QThread>
#include <QVBoxLayout>

namespace qssh {

// Runs @p fn on the GUI thread and waits for it.
template <typename Fn>
static void runOnGuiThread(Fn fn)
{
    QCoreApplication *app = QCoreApplication::instance();
    if (!app || QThread::currentThread() == app->thread())
        fn();
    else
        QMetaObject::invokeMethod(app, fn, Qt::BlockingQueuedConnection);
}

PromptResult askText(const QString &title, const QString &instruction,
                     const QString &label, bool echo, bool offerSave)
{
    PromptResult result;
    runOnGuiThread([&]() {
        QDialog dlg(QApplication::activeWindow());
        dlg.setWindowTitle(title);
        QVBoxLayout *layout = new QVBoxLayout(&dlg);
        if (!instruction.isEmpty()) {
            QLabel *info = new QLabel(instruction, &dlg);
            info->setWordWrap(true);
            info->setTextInteractionFlags(Qt::TextSelectableByMouse);
            layout->addWidget(info);
        }
        QLineEdit *edit = new QLineEdit(&dlg);
        if (!echo)
            edit->setEchoMode(QLineEdit::Password);
        edit->setMinimumWidth(280);
        QFormLayout *form = new QFormLayout;
        QString text = label.trimmed();
        if (text.endsWith(':'))
            text.chop(1);
        form->addRow(text.isEmpty() ? QObject::tr("Response") : text, edit);
        layout->addLayout(form);
        QCheckBox *save = nullptr;
        if (offerSave) {
            save = new QCheckBox(QObject::tr("Save password (stored in ~/.qsshterm/sites.xml)"), &dlg);
            layout->addWidget(save);
        }
        QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        layout->addWidget(buttons);
        edit->setFocus();
        if (dlg.exec() == QDialog::Accepted) {
            result.ok = true;
            result.text = edit->text();
            result.save = save && save->isChecked();
        }
    });
    return result;
}

bool askYesNo(const QString &title, const QString &text)
{
    bool yes = false;
    runOnGuiThread([&]() {
        yes = QMessageBox::question(QApplication::activeWindow(), title, text,
                                    QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
              == QMessageBox::Yes;
    });
    return yes;
}

} // namespace qssh

#include "qsite_editor.h"

#include <QtWidgets>
#include <qtermwidget6/qtermwidget.h>

namespace qssh {

namespace {

QWidget *withBrowse(QLineEdit *edit, QWidget *parent, const QString &caption, bool directory = false)
{
    QWidget *w = new QWidget(parent);
    QHBoxLayout *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->addWidget(edit);
    QPushButton *browse = new QPushButton(QObject::tr("Browse..."), w);
    l->addWidget(browse);
    QObject::connect(browse, &QPushButton::clicked, w, [w, edit, caption, directory]() {
        QString start = edit->text();
        if (start.startsWith(QLatin1String("~/")))
            start = QDir::homePath() + start.mid(1);
        if (start.isEmpty())
            start = QDir::homePath() + QStringLiteral("/.ssh");
        const QString file = directory ? QFileDialog::getExistingDirectory(w, caption, start)
                                       : QFileDialog::getOpenFileName(w, caption, start);
        if (!file.isEmpty())
            edit->setText(file);
    });
    return w;
}

QLineEdit *lineEdit(const QString &text, QWidget *parent, const QString &placeholder = QString())
{
    QLineEdit *e = new QLineEdit(text, parent);
    e->setPlaceholderText(placeholder);
    e->setClearButtonEnabled(true);
    return e;
}

QSpinBox *spin(int min, int max, int value, const QString &special, const QString &suffix, QWidget *parent)
{
    QSpinBox *s = new QSpinBox(parent);
    s->setRange(min, max);
    s->setSpecialValueText(special);
    s->setSuffix(suffix);
    s->setValue(value);
    return s;
}

void selectData(QComboBox *combo, const QVariant &data)
{
    const int i = combo->findData(data);
    combo->setCurrentIndex(i >= 0 ? i : 0);
}

// Table with Add / Remove buttons underneath
QWidget *tableWithButtons(QTableWidget *table, QWidget *parent, std::function<void()> addRow)
{
    QWidget *w = new QWidget(parent);
    QVBoxLayout *l = new QVBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->hide();
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    l->addWidget(table);
    QHBoxLayout *buttons = new QHBoxLayout;
    QPushButton *add = new QPushButton(QObject::tr("Add"), w);
    QPushButton *remove = new QPushButton(QObject::tr("Remove"), w);
    buttons->addWidget(add);
    buttons->addWidget(remove);
    buttons->addStretch();
    l->addLayout(buttons);
    QObject::connect(add, &QPushButton::clicked, w, [table, addRow]() {
        addRow();
        table->setCurrentCell(table->rowCount() - 1, 0);
        table->editItem(table->item(table->rowCount() - 1, 1));
    });
    QObject::connect(remove, &QPushButton::clicked, w, [table]() {
        const int row = table->currentRow();
        if (row >= 0)
            table->removeRow(row);
    });
    return w;
}

QString cellText(QTableWidget *t, int row, int column)
{
    QTableWidgetItem *item = t->item(row, column);
    return item ? item->text().trimmed() : QString();
}

} // namespace

bool editSite(QWidget *parent, const QString &title, SiteInfo &info, const QStringList &sessionNames)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    QTabWidget *tabs = new QTabWidget(&dlg);

    // ---------------------------------------------------------------- Connection
    QWidget *connPage = new QWidget;
    QVBoxLayout *connLayout = new QVBoxLayout(connPage);
    QFormLayout *connForm = new QFormLayout;
    QLineEdit *name = lineEdit(info.sitename, connPage, QObject::tr("defaults to the host"));
    QLineEdit *host = lineEdit(info.hostname, connPage, QObject::tr("host name, IP or ~/.ssh/config alias"));
    QSpinBox *port = spin(1, 65535, info.port > 0 ? info.port : 22, QString(), QString(), connPage);
    QLineEdit *user = lineEdit(info.username, connPage, QObject::tr("from ~/.ssh/config or your login name"));
    QComboBox *jump = new QComboBox(connPage);
    jump->setEditable(true);
    jump->addItem(QString());
    jump->addItems(sessionNames);
    jump->setCurrentText(info.jumpHosts);
    jump->lineEdit()->setPlaceholderText(QObject::tr("none"));
    jump->setToolTip(QObject::tr("Connect through these hosts first (ProxyJump).\n"
                                 "A saved session name or [user@]host[:port];\n"
                                 "separate several hops with commas."));
    connForm->addRow(QObject::tr("Name:"), name);
    connForm->addRow(QObject::tr("Host:"), host);
    connForm->addRow(QObject::tr("Port:"), port);
    connForm->addRow(QObject::tr("Username:"), user);
    connForm->addRow(QObject::tr("Jump hosts:"), jump);
    connLayout->addLayout(connForm);

    QGroupBox *proxyBox = new QGroupBox(QObject::tr("Proxy"), connPage);
    QFormLayout *proxyForm = new QFormLayout(proxyBox);
    QComboBox *proxyType = new QComboBox(proxyBox);
    proxyType->addItem(QObject::tr("None (direct)"), SiteInfo::NoProxy);
    proxyType->addItem(QObject::tr("SOCKS5"), SiteInfo::Socks5);
    proxyType->addItem(QObject::tr("SOCKS4 / 4a"), SiteInfo::Socks4);
    proxyType->addItem(QObject::tr("HTTP CONNECT"), SiteInfo::Http);
#ifndef Q_OS_WIN
    proxyType->addItem(QObject::tr("Command (ProxyCommand)"), SiteInfo::ProxyCommand);
#endif
    selectData(proxyType, info.proxyType);
    QLineEdit *proxyHost = lineEdit(info.proxyHost, proxyBox);
    QSpinBox *proxyPort = spin(0, 65535, info.proxyPort, QObject::tr("-"), QString(), proxyBox);
    QLineEdit *proxyUser = lineEdit(info.proxyUser, proxyBox, QObject::tr("optional"));
    QLineEdit *proxyPass = lineEdit(info.proxyPassword, proxyBox, QObject::tr("optional"));
    proxyPass->setEchoMode(QLineEdit::Password);
    QLineEdit *proxyCommand = lineEdit(info.proxyCommand, proxyBox, QObject::tr("e.g. nc -X connect -x proxy:3128 %h %p"));
    QHBoxLayout *proxyAddr = new QHBoxLayout;
    proxyAddr->addWidget(proxyHost, 1);
    proxyAddr->addWidget(new QLabel(QObject::tr("Port:"), proxyBox));
    proxyAddr->addWidget(proxyPort);
    proxyForm->addRow(QObject::tr("Type:"), proxyType);
    proxyForm->addRow(QObject::tr("Host:"), proxyAddr);
    proxyForm->addRow(QObject::tr("Username:"), proxyUser);
    proxyForm->addRow(QObject::tr("Password:"), proxyPass);
    proxyForm->addRow(QObject::tr("Command:"), proxyCommand);
    auto updateProxy = [=]() {
        const int t = proxyType->currentData().toInt();
        const bool network = t == SiteInfo::Socks5 || t == SiteInfo::Socks4 || t == SiteInfo::Http;
        proxyHost->setEnabled(network);
        proxyPort->setEnabled(network);
        proxyUser->setEnabled(network);
        proxyPass->setEnabled(t == SiteInfo::Socks5 || t == SiteInfo::Http);
        proxyCommand->setEnabled(t == SiteInfo::ProxyCommand);
    };
    QObject::connect(proxyType, &QComboBox::currentIndexChanged, &dlg, updateProxy);
    updateProxy();
    connLayout->addWidget(proxyBox);

    QGroupBox *netBox = new QGroupBox(QObject::tr("Network"), connPage);
    QFormLayout *netForm = new QFormLayout(netBox);
    QSpinBox *timeout = spin(0, 600, info.connectTimeout, QObject::tr("Default"), QObject::tr(" s"), netBox);
    QSpinBox *keepAlive = spin(0, 3600, info.keepAliveInterval, QObject::tr("Off"), QObject::tr(" s"), netBox);
    keepAlive->setToolTip(QObject::tr("Send an SSH keepalive this often so idle sessions survive NAT and firewalls."));
    QComboBox *family = new QComboBox(netBox);
    family->addItem(QObject::tr("Any"), SiteInfo::AnyFamily);
    family->addItem(QObject::tr("IPv4 only"), SiteInfo::IPv4);
    family->addItem(QObject::tr("IPv6 only"), SiteInfo::IPv6);
    selectData(family, info.addressFamily);
    QLineEdit *bindAddress = lineEdit(info.bindAddress, netBox, QObject::tr("any local address"));
    QCheckBox *sshConfig = new QCheckBox(QObject::tr("Read ~/.ssh/config for settings left empty here"), netBox);
    sshConfig->setChecked(info.useSshConfig);
    QLineEdit *sftpDir = lineEdit(info.sftpStartDir, netBox, QObject::tr("remote home folder"));
    netForm->addRow(QObject::tr("Connect timeout:"), timeout);
    netForm->addRow(QObject::tr("Keepalive interval:"), keepAlive);
    netForm->addRow(QObject::tr("Address family:"), family);
    netForm->addRow(QObject::tr("Local bind address:"), bindAddress);
    netForm->addRow(QObject::tr("SFTP start folder:"), sftpDir);
    netForm->addRow(sshConfig);
    connLayout->addWidget(netBox);
    connLayout->addStretch();
    tabs->addTab(connPage, QObject::tr("Connection"));

    // ------------------------------------------------------------ Authentication
    QWidget *authPage = new QWidget;
    QVBoxLayout *authLayout = new QVBoxLayout(authPage);
    QFormLayout *authForm = new QFormLayout;
    QLineEdit *pass = lineEdit(info.password, authPage, QObject::tr("leave empty to be asked when connecting"));
    pass->setEchoMode(QLineEdit::Password);
    QLineEdit *key = lineEdit(info.privateKeyPath, authPage, QObject::tr("default keys and ssh-agent are tried too"));
    QLineEdit *cert = lineEdit(info.certificatePath, authPage, QObject::tr("<key>-cert.pub if present"));
    authForm->addRow(QObject::tr("Password:"), pass);
    authForm->addRow(QObject::tr("Private key:"), withBrowse(key, authPage, QObject::tr("Select private key")));
    authForm->addRow(QObject::tr("Certificate:"), withBrowse(cert, authPage, QObject::tr("Select certificate")));
    authLayout->addLayout(authForm);
    QLabel *passNote = new QLabel(QObject::tr("Saved passwords are stored unencrypted in ~/.qsshterm/sites.xml "
                                              "(readable only by you)."), authPage);
    passNote->setWordWrap(true);
    passNote->setEnabled(false);
    authLayout->addWidget(passNote);

    QGroupBox *methodBox = new QGroupBox(QObject::tr("Methods, tried in this order"), authPage);
    QVBoxLayout *methodLayout = new QVBoxLayout(methodBox);
    QCheckBox *mGssapi = new QCheckBox(QObject::tr("GSSAPI (Kerberos)"), methodBox);
    QCheckBox *mKey = new QCheckBox(QObject::tr("Public key files (the key above, IdentityFile, ~/.ssh/id_*)"), methodBox);
    QCheckBox *mAgent = new QCheckBox(QObject::tr("SSH agent"), methodBox);
    QCheckBox *mPassword = new QCheckBox(QObject::tr("Password"), methodBox);
    QCheckBox *mKbd = new QCheckBox(QObject::tr("Keyboard-interactive (password prompts, 2FA / OTP codes)"), methodBox);
    QCheckBox *delegate = new QCheckBox(QObject::tr("Delegate Kerberos credentials (GSSAPIDelegateCredentials)"), methodBox);
    mGssapi->setChecked(info.authGssapi);
    mKey->setChecked(info.authPublicKey);
    mAgent->setChecked(info.authAgent);
    mPassword->setChecked(info.authPassword);
    mKbd->setChecked(info.authKeyboardInteractive);
    delegate->setChecked(info.gssapiDelegate);
    for (QCheckBox *c : {mGssapi, mKey, mAgent, mPassword, mKbd})
        methodLayout->addWidget(c);
    methodLayout->addSpacing(6);
    methodLayout->addWidget(delegate);
    authLayout->addWidget(methodBox);

    QFormLayout *agentForm = new QFormLayout;
    QLineEdit *agentSocket = lineEdit(info.agentSocket, authPage,
#ifdef Q_OS_WIN
                                      QObject::tr("\\\\.\\pipe\\openssh-ssh-agent")
#else
                                      QObject::tr("$SSH_AUTH_SOCK")
#endif
    );
    agentForm->addRow(QObject::tr("Agent socket:"), agentSocket);
    authLayout->addLayout(agentForm);
    authLayout->addStretch();
    tabs->addTab(authPage, QObject::tr("Authentication"));

    // ---------------------------------------------------------------- Forwarding
    QWidget *fwPage = new QWidget;
    QVBoxLayout *fwLayout = new QVBoxLayout(fwPage);
    QCheckBox *x11 = new QCheckBox(QObject::tr("Forward X11 (run remote graphical programs on this display)"), fwPage);
    x11->setChecked(info.transferX11);
    QCheckBox *agentFw = new QCheckBox(QObject::tr("Forward the SSH agent (use your local keys from the server)"), fwPage);
    agentFw->setChecked(info.forwardAgent);
    agentFw->setToolTip(QObject::tr("Only for servers you trust: their administrators can use your agent while you are connected."));
    fwLayout->addWidget(x11);
    fwLayout->addWidget(agentFw);
    fwLayout->addWidget(new QLabel(QObject::tr("Port forwarding:"), fwPage));

    QTableWidget *fwTable = new QTableWidget(0, 5, fwPage);
    fwTable->setHorizontalHeaderLabels({QObject::tr("Type"), QObject::tr("Listen address"), QObject::tr("Listen port"),
                                        QObject::tr("Destination host"), QObject::tr("Destination port")});
    auto addForward = [fwTable](const PortForward &f) {
        const int row = fwTable->rowCount();
        fwTable->insertRow(row);
        QComboBox *type = new QComboBox(fwTable);
        type->addItem(QObject::tr("Local (-L)"), PortForward::Local);
        type->addItem(QObject::tr("Remote (-R)"), PortForward::Remote);
        type->addItem(QObject::tr("Dynamic SOCKS (-D)"), PortForward::Dynamic);
        selectData(type, int(f.type));
        fwTable->setCellWidget(row, 0, type);
        fwTable->setItem(row, 1, new QTableWidgetItem(f.bindAddress));
        fwTable->setItem(row, 2, new QTableWidgetItem(f.bindPort > 0 ? QString::number(f.bindPort) : QString()));
        fwTable->setItem(row, 3, new QTableWidgetItem(f.destHost));
        fwTable->setItem(row, 4, new QTableWidgetItem(f.destPort > 0 ? QString::number(f.destPort) : QString()));
    };
    for (const PortForward &f : info.forwards)
        addForward(f);
    fwTable->resizeColumnsToContents();
    fwLayout->addWidget(tableWithButtons(fwTable, fwPage, [addForward]() { addForward(PortForward()); }));
    QLabel *fwNote = new QLabel(QObject::tr("Local and Dynamic listen on this computer (empty address: localhost, "
                                            "* : all interfaces). Remote listens on the server and connects to "
                                            "the destination from this computer."), fwPage);
    fwNote->setWordWrap(true);
    fwNote->setEnabled(false);
    fwLayout->addWidget(fwNote);
    tabs->addTab(fwPage, QObject::tr("Forwarding"));

    // ------------------------------------------------------------------ Terminal
    QWidget *termPage = new QWidget;
    QVBoxLayout *termLayout = new QVBoxLayout(termPage);
    QFormLayout *termForm = new QFormLayout;
    QComboBox *termType = new QComboBox(termPage);
    termType->setEditable(true);
    termType->addItems({QStringLiteral("xterm"), QStringLiteral("xterm-256color"), QStringLiteral("vt100"),
                        QStringLiteral("vt220"), QStringLiteral("linux"), QStringLiteral("screen-256color"),
                        QStringLiteral("tmux-256color")});
    termType->setCurrentText(info.termType.isEmpty() ? QStringLiteral("xterm") : info.termType);
    QComboBox *encoding = new QComboBox(termPage);
    encoding->addItem(QObject::tr("UTF-8"), QString());
    encoding->addItem(QObject::tr("ISO-8859-1 (Latin-1)"), QStringLiteral("Latin1"));
    encoding->addItem(QObject::tr("System locale"), QStringLiteral("System"));
    selectData(encoding, info.encoding);
    QComboBox *colors = new QComboBox(termPage);
    colors->addItem(QObject::tr("Follow the app theme"), QString());
    QStringList schemes = QTermWidget::availableColorSchemes();
    schemes.sort(Qt::CaseInsensitive);
    for (const QString &s : schemes)
        colors->addItem(s, s);
    selectData(colors, info.colorScheme);

    QHBoxLayout *fontRow = new QHBoxLayout;
    QCheckBox *customFont = new QCheckBox(QObject::tr("Custom:"), termPage);
    QFontComboBox *font = new QFontComboBox(termPage);
    font->setFontFilters(QFontComboBox::MonospacedFonts);
    if (!info.fontFamily.isEmpty())
        font->setCurrentFont(QFont(info.fontFamily));
    customFont->setChecked(!info.fontFamily.isEmpty());
    font->setEnabled(customFont->isChecked());
    QObject::connect(customFont, &QCheckBox::toggled, font, &QWidget::setEnabled);
    QSpinBox *fontSize = spin(0, 72, info.fontSize, QObject::tr("Default size"), QObject::tr(" pt"), termPage);
    fontRow->addWidget(customFont);
    fontRow->addWidget(font, 1);
    fontRow->addWidget(fontSize);

    QHBoxLayout *scrollRow = new QHBoxLayout;
    QComboBox *scrollMode = new QComboBox(termPage);
    scrollMode->addItem(QObject::tr("Default (1000 lines)"), -2);
    scrollMode->addItem(QObject::tr("Unlimited"), -1);
    scrollMode->addItem(QObject::tr("Lines:"), 0);
    QSpinBox *scrollLines = spin(0, 10000000, info.scrollback >= 0 ? info.scrollback : 10000, QString(), QString(), termPage);
    selectData(scrollMode, info.scrollback >= 0 ? 0 : info.scrollback);
    auto updateScroll = [=]() { scrollLines->setEnabled(scrollMode->currentData().toInt() == 0); };
    QObject::connect(scrollMode, &QComboBox::currentIndexChanged, &dlg, updateScroll);
    updateScroll();
    scrollRow->addWidget(scrollMode);
    scrollRow->addWidget(scrollLines, 1);

    QComboBox *keyboard = new QComboBox(termPage);
    keyboard->addItem(QObject::tr("Default"), QString());
    QStringList layouts = QTermWidget::availableKeyBindings();
    layouts.sort(Qt::CaseInsensitive);
    for (const QString &l : layouts)
        keyboard->addItem(l, l);
    selectData(keyboard, info.keyboardLayout);
    QLineEdit *remoteCommand = lineEdit(info.remoteCommand, termPage, QObject::tr("login shell"));
    remoteCommand->setToolTip(QObject::tr("Run this command instead of a shell (like ssh host command)."));
    QPlainTextEdit *startup = new QPlainTextEdit(info.startupCommands, termPage);
    startup->setPlaceholderText(QObject::tr("Typed into the shell after login, one command per line"));
    startup->setMaximumHeight(80);

    termForm->addRow(QObject::tr("Terminal type (TERM):"), termType);
    termForm->addRow(QObject::tr("Character encoding:"), encoding);
    termForm->addRow(QObject::tr("Colour scheme:"), colors);
    termForm->addRow(QObject::tr("Font:"), fontRow);
    termForm->addRow(QObject::tr("Scrollback:"), scrollRow);
    termForm->addRow(QObject::tr("Keyboard layout:"), keyboard);
    termForm->addRow(QObject::tr("Remote command:"), remoteCommand);
    termForm->addRow(QObject::tr("Startup commands:"), startup);
    termLayout->addLayout(termForm);

    termLayout->addWidget(new QLabel(QObject::tr("Environment variables (the server must allow them with AcceptEnv):"), termPage));
    QTableWidget *envTable = new QTableWidget(0, 2, termPage);
    envTable->setHorizontalHeaderLabels({QObject::tr("Name"), QObject::tr("Value")});
    auto addEnv = [envTable](const QString &n, const QString &v) {
        const int row = envTable->rowCount();
        envTable->insertRow(row);
        envTable->setItem(row, 0, new QTableWidgetItem(n));
        envTable->setItem(row, 1, new QTableWidgetItem(v));
    };
    for (const auto &var : info.environment)
        addEnv(var.first, var.second);
    envTable->setColumnWidth(0, 160);
    QWidget *envWidget = tableWithButtons(envTable, termPage, [addEnv, envTable]() {
        addEnv(QString(), QString());
        envTable->editItem(envTable->item(envTable->rowCount() - 1, 0));
    });
    termLayout->addWidget(envWidget);
    tabs->addTab(termPage, QObject::tr("Terminal"));

    // ------------------------------------------------------------------ Advanced
    QWidget *advPage = new QWidget;
    QVBoxLayout *advLayout = new QVBoxLayout(advPage);
    QFormLayout *advForm = new QFormLayout;
    QComboBox *hostKeyCheck = new QComboBox(advPage);
    hostKeyCheck->addItem(QObject::tr("Ask for unknown hosts"), SiteInfo::HostKeyAsk);
    hostKeyCheck->addItem(QObject::tr("Accept and remember unknown hosts"), SiteInfo::HostKeyAcceptNew);
    hostKeyCheck->addItem(QObject::tr("Strict: refuse unknown hosts"), SiteInfo::HostKeyStrict);
    hostKeyCheck->addItem(QObject::tr("Off: never check (insecure)"), SiteInfo::HostKeyOff);
    selectData(hostKeyCheck, info.hostKeyCheck);
    QLineEdit *knownHosts = lineEdit(info.knownHostsFile, advPage, QStringLiteral("~/.ssh/known_hosts"));
    QComboBox *compression = new QComboBox(advPage);
    compression->addItem(QObject::tr("Default"), -1);
    compression->addItem(QObject::tr("On"), 1);
    compression->addItem(QObject::tr("Off"), 0);
    selectData(compression, info.compression);
    const QString defaultList = QObject::tr("default; comma separated, e.g. +ssh-rsa to add one");
    QLineEdit *ciphers = lineEdit(info.ciphers, advPage, defaultList);
    QLineEdit *kex = lineEdit(info.kexAlgorithms, advPage, defaultList);
    QLineEdit *macs = lineEdit(info.macs, advPage, defaultList);
    QLineEdit *hostKeys = lineEdit(info.hostKeyAlgorithms, advPage, defaultList);
    QLineEdit *pubkeyTypes = lineEdit(info.pubkeyAcceptedTypes, advPage, defaultList);
    ciphers->setToolTip(QObject::tr("Ciphers, e.g. aes128-ctr,aes256-ctr for old network devices"));
    kex->setToolTip(QObject::tr("KexAlgorithms, e.g. +diffie-hellman-group14-sha1"));
    hostKeys->setToolTip(QObject::tr("HostKeyAlgorithms, e.g. +ssh-rsa"));
    pubkeyTypes->setToolTip(QObject::tr("PubkeyAcceptedKeyTypes, e.g. +ssh-rsa"));
    QComboBox *logLevel = new QComboBox(advPage);
    logLevel->addItem(QObject::tr("None"), 0);
    logLevel->addItem(QObject::tr("Warnings"), 1);
    logLevel->addItem(QObject::tr("Protocol"), 2);
    logLevel->addItem(QObject::tr("Packets"), 3);
    logLevel->addItem(QObject::tr("Functions (very verbose)"), 4);
    selectData(logLevel, info.logLevel);
    logLevel->setToolTip(QObject::tr("libssh debug output, written to the console qsshterm was started from"));
    advForm->addRow(QObject::tr("Host key checking:"), hostKeyCheck);
    advForm->addRow(QObject::tr("Known hosts file:"), withBrowse(knownHosts, advPage, QObject::tr("Select known hosts file")));
    advForm->addRow(QObject::tr("Compression:"), compression);
    advForm->addRow(QObject::tr("Ciphers:"), ciphers);
    advForm->addRow(QObject::tr("Key exchange:"), kex);
    advForm->addRow(QObject::tr("MACs:"), macs);
    advForm->addRow(QObject::tr("Host key algorithms:"), hostKeys);
    advForm->addRow(QObject::tr("Public key types:"), pubkeyTypes);
    advForm->addRow(QObject::tr("Debug log level:"), logLevel);
    advLayout->addLayout(advForm);
    advLayout->addStretch();
    tabs->addTab(advPage, QObject::tr("Advanced"));

    // ------------------------------------------------------------------- buttons
    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    auto problem = [&](QWidget *page, QWidget *field, const QString &text) {
        tabs->setCurrentWidget(page);
        if (field)
            field->setFocus();
        QMessageBox::warning(&dlg, dlg.windowTitle(), text);
    };
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, [&]() {
        if (host->text().trimmed().isEmpty())
            return problem(connPage, host, QObject::tr("Host must not be empty."));
        const int pt = proxyType->currentData().toInt();
        if ((pt == SiteInfo::Socks5 || pt == SiteInfo::Socks4 || pt == SiteInfo::Http)
            && (proxyHost->text().trimmed().isEmpty() || proxyPort->value() == 0))
            return problem(connPage, proxyHost, QObject::tr("Enter the proxy host and port."));
        if (pt == SiteInfo::ProxyCommand && proxyCommand->text().trimmed().isEmpty())
            return problem(connPage, proxyCommand, QObject::tr("Enter the proxy command."));
        for (int row = 0; row < fwTable->rowCount(); ++row) {
            const int type = qobject_cast<QComboBox *>(fwTable->cellWidget(row, 0))->currentData().toInt();
            const int listen = cellText(fwTable, row, 2).toInt();
            const int dport = cellText(fwTable, row, 4).toInt();
            if (listen < 0 || listen > 65535 || (listen == 0 && type != PortForward::Remote))
                return problem(fwPage, fwTable, QObject::tr("Port forward %1: enter a listen port (1-65535).").arg(row + 1));
            if (type != PortForward::Dynamic && (cellText(fwTable, row, 3).isEmpty() || dport <= 0 || dport > 65535))
                return problem(fwPage, fwTable, QObject::tr("Port forward %1: enter the destination host and port.").arg(row + 1));
        }
        dlg.accept();
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    QVBoxLayout *layout = new QVBoxLayout(&dlg);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
    dlg.resize(620, 600);
    host->setFocus();

    if (dlg.exec() != QDialog::Accepted)
        return false;

    // ---------------------------------------------------------------- read back
    info.hostname = host->text().trimmed();
    info.sitename = name->text().trimmed().isEmpty() ? info.hostname : name->text().trimmed();
    info.port = port->value();
    info.username = user->text().trimmed();
    info.jumpHosts = jump->currentText().trimmed();
    info.proxyType = proxyType->currentData().toInt();
    info.proxyHost = proxyHost->text().trimmed();
    info.proxyPort = proxyPort->value();
    info.proxyUser = proxyUser->text().trimmed();
    info.proxyPassword = proxyPass->text();
    info.proxyCommand = proxyCommand->text().trimmed();
    info.connectTimeout = timeout->value();
    info.keepAliveInterval = keepAlive->value();
    info.addressFamily = family->currentData().toInt();
    info.bindAddress = bindAddress->text().trimmed();
    info.useSshConfig = sshConfig->isChecked();
    info.sftpStartDir = sftpDir->text().trimmed();

    info.password = pass->text();
    info.privateKeyPath = key->text().trimmed();
    info.certificatePath = cert->text().trimmed();
    info.authGssapi = mGssapi->isChecked();
    info.authPublicKey = mKey->isChecked();
    info.authAgent = mAgent->isChecked();
    info.authPassword = mPassword->isChecked();
    info.authKeyboardInteractive = mKbd->isChecked();
    info.gssapiDelegate = delegate->isChecked();
    info.agentSocket = agentSocket->text().trimmed();

    info.transferX11 = x11->isChecked();
    info.forwardAgent = agentFw->isChecked();
    info.forwards.clear();
    for (int row = 0; row < fwTable->rowCount(); ++row) {
        PortForward f;
        f.type = PortForward::Type(qobject_cast<QComboBox *>(fwTable->cellWidget(row, 0))->currentData().toInt());
        f.bindAddress = cellText(fwTable, row, 1);
        f.bindPort = cellText(fwTable, row, 2).toInt();
        if (f.type != PortForward::Dynamic) {
            f.destHost = cellText(fwTable, row, 3);
            f.destPort = cellText(fwTable, row, 4).toInt();
        }
        info.forwards << f;
    }

    const QString term = termType->currentText().trimmed();
    info.termType = term == QLatin1String("xterm") ? QString() : term;
    info.encoding = encoding->currentData().toString();
    info.colorScheme = colors->currentData().toString();
    info.fontFamily = customFont->isChecked() ? font->currentFont().family() : QString();
    info.fontSize = fontSize->value();
    info.scrollback = scrollMode->currentData().toInt() == 0 ? scrollLines->value() : scrollMode->currentData().toInt();
    info.keyboardLayout = keyboard->currentData().toString();
    info.remoteCommand = remoteCommand->text().trimmed();
    info.startupCommands = startup->toPlainText();
    info.environment.clear();
    for (int row = 0; row < envTable->rowCount(); ++row) {
        const QString n = cellText(envTable, row, 0);
        if (!n.isEmpty())
            info.environment << qMakePair(n, envTable->item(row, 1) ? envTable->item(row, 1)->text() : QString());
    }

    info.hostKeyCheck = hostKeyCheck->currentData().toInt();
    info.knownHostsFile = knownHosts->text().trimmed();
    info.compression = compression->currentData().toInt();
    info.ciphers = ciphers->text().trimmed();
    info.kexAlgorithms = kex->text().trimmed();
    info.macs = macs->text().trimmed();
    info.hostKeyAlgorithms = hostKeys->text().trimmed();
    info.pubkeyAcceptedTypes = pubkeyTypes->text().trimmed();
    info.logLevel = logLevel->currentData().toInt();
    return true;
}

} // namespace qssh

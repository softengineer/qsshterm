#include "qssh_theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <qtermwidget6/qtermwidget.h>

static QSettings settings()
{
    return QSettings(QDir::homePath() + "/.qsshterm/qsshterm.ini", QSettings::IniFormat);
}

static QPalette lightPalette()
{
    return QApplication::style()->standardPalette();
}

static QPalette darkPalette()
{
    const QColor window(0x2b, 0x2b, 0x2b);
    const QColor base(0x1f, 0x1f, 0x1f);
    const QColor alternate(0x26, 0x26, 0x26);
    const QColor text(0xe6, 0xe6, 0xe6);
    const QColor disabled(0x7f, 0x7f, 0x7f);
    const QColor highlight(0x2f, 0x6f, 0xb5);

    QPalette p;
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, base);
    p.setColor(QPalette::AlternateBase, alternate);
    p.setColor(QPalette::ToolTipBase, QColor(0x3a, 0x3a, 0x3a));
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, disabled);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, QColor(0x35, 0x35, 0x35));
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Light, QColor(0x45, 0x45, 0x45));
    p.setColor(QPalette::Midlight, QColor(0x3a, 0x3a, 0x3a));
    p.setColor(QPalette::Mid, QColor(0x30, 0x30, 0x30));
    p.setColor(QPalette::Dark, QColor(0x18, 0x18, 0x18));
    p.setColor(QPalette::Shadow, Qt::black);
    p.setColor(QPalette::Highlight, highlight);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, QColor(0x5d, 0xa9, 0xf6));
    p.setColor(QPalette::LinkVisited, QColor(0xb0, 0x8c, 0xf0));
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText,
                                     QPalette::HighlightedText})
        p.setColor(QPalette::Disabled, role, disabled);
    p.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x45, 0x45, 0x45));
    return p;
}

ThemeManager *ThemeManager::instance()
{
    static ThemeManager *mgr = new ThemeManager;
    return mgr;
}

void ThemeManager::init()
{
    // Fusion renders both palettes properly on every platform
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    // bundled Breeze subset (qsshterm.qrc): the icon theme on Windows/macOS,
    // the fallback for names the desktop theme lacks on Linux
    QIcon::setThemeSearchPaths(QIcon::themeSearchPaths() << QStringLiteral(":/icons"));
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    systemIconTheme.clear();
#else
    systemIconTheme = QIcon::themeName();
    if (systemIconTheme == QLatin1String("hicolor"))
        systemIconTheme.clear();
#endif

    QSettings s = settings();
    const QString t = s.value("appearance/theme", "System").toString();
    current = t == "Dark" ? Dark : t == "Light" ? Light : System;
    termScheme = s.value("appearance/terminalScheme", "Auto").toString();

    // follow the desktop when it switches between light and dark
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this]() {
        if (current == System)
            apply();
    });
    apply();
}

void ThemeManager::setTheme(Theme theme)
{
    current = theme;
    settings().setValue("appearance/theme",
                        theme == Dark ? "Dark" : theme == Light ? "Light" : "System");
    apply();
}

void ThemeManager::setTerminalScheme(const QString &scheme)
{
    termScheme = scheme;
    settings().setValue("appearance/terminalScheme", scheme);
    emit changed();
}

QString ThemeManager::effectiveTerminalScheme() const
{
    if (termScheme != "Auto" && availableTerminalSchemes().contains(termScheme))
        return termScheme;
    return dark ? QStringLiteral("Ubuntu") : QStringLiteral("BlackOnWhite");
}

QStringList ThemeManager::availableTerminalSchemes()
{
    QStringList list = QTermWidget::availableColorSchemes();
    list.removeDuplicates();
    list.sort(Qt::CaseInsensitive);
    return list;
}

void ThemeManager::apply()
{
    dark = current == Dark
        || (current == System && QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark);

    QApplication::setPalette(dark ? darkPalette() : lightPalette());

    const QString bundled = dark ? QStringLiteral("qsshterm-dark") : QStringLiteral("qsshterm");
    QIcon::setFallbackThemeName(bundled);
    if (systemIconTheme.isEmpty()) {
        QIcon::setThemeName(bundled);
    } else {
        // switch to the "-dark" variant of the desktop theme (e.g. Yaru-red-dark)
        // so toolbar icons stay visible on a dark background
        QString iconTheme = systemIconTheme;
        if (iconTheme.endsWith("-dark"))
            iconTheme.chop(5);
        if (dark) {
            const QString variant = iconTheme + "-dark";
            for (const QString &dir : QIcon::themeSearchPaths()) {
                if (QFile::exists(dir + '/' + variant + "/index.theme")) {
                    iconTheme = variant;
                    break;
                }
            }
        }
        QIcon::setThemeName(iconTheme);
    }

    emit changed();
}

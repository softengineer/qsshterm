#ifndef QSSH_THEME_H
#define QSSH_THEME_H

#include <QObject>
#include <QString>
#include <QStringList>

/**
 * Application colour theme (System / Light / Dark) and the terminal colour
 * scheme that goes with it. Choices are stored in ~/.qsshterm/qsshterm.ini.
 */
class ThemeManager : public QObject
{
    Q_OBJECT

public:
    enum Theme { System, Light, Dark };
    Q_ENUM(Theme)

    static ThemeManager *instance();

    /** Applies the saved theme; call once after QApplication exists. */
    void init();

    Theme theme() const { return current; }
    void setTheme(Theme theme);
    /** True when the effective palette is dark (System resolved). */
    bool isDark() const { return dark; }

    /** "Auto" or a qtermwidget colour scheme name. */
    QString terminalSchemeSetting() const { return termScheme; }
    void setTerminalScheme(const QString &scheme);
    /** Scheme to use right now: the chosen one, or the theme default for "Auto". */
    QString effectiveTerminalScheme() const;
    static QStringList availableTerminalSchemes();

signals:
    /** Palette and/or terminal scheme changed; terminals should re-apply. */
    void changed();

private:
    ThemeManager() = default;
    void apply();

    Theme current = System;
    bool dark = false;
    QString termScheme = QStringLiteral("Auto");
    QString systemIconTheme;
};

#endif

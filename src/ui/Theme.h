#pragma once

#include <QColor>
#include <QPalette>
#include <QString>

// Colours, button styles and the application palette shared by the log viewer and
// the mods panel.
//
// Two palettes are built in, a light and a dark one. Widgets never hard-code a
// colour: they ask the current palette and restyle themselves when the user
// switches the theme (MainWindow::applyTheme(), ModsPanel::applyTheme()), so both
// parts of the window - and the dialogs Qt draws itself - stay consistent.
namespace ui {

enum class ThemeMode
{
    Light,
    Dark
};

struct Palette
{
    QString background;       // window background
    QString surface;          // lists, text areas, cards
    QString text;             // normal text
    QString muted;            // secondary text
    QString border;
    QString accent;           // error red: error labels and panel edges
    QString accentSoft;       // amber: warnings and counters
    QString primary;          // the action the user is expected to take
    QString primaryHover;
    QString primaryText;
    QString neutral;          // plain buttons
    QString neutralHover;
    QString disabledSurface;
    QString disabledText;
    QString disabledBorder;
    QString danger;           // destructive buttons
    QString dangerHover;
    QString dangerBorder;
    QString dangerText;
    QString errorPane;        // background of the resolved compile error panel
    QString logLine;          // the highlighted compile error line
    QString logLineText;
    QString errorPrefix;      // the "ERROR" at the start of a log line
    QString progressTrack;
};

// QColor from a "#rrggbb" string. QColor(QString) is the only spelling Qt 5.15
// has, so the newer fromString() is used where it exists.
inline QColor ToColor(const QString& value)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    return QColor::fromString(value);
#else
    return QColor(value);
#endif
}

inline const Palette& LightPalette()
{
    static const Palette palette{
        .background = QStringLiteral("#f4f7fb"),
        .surface = QStringLiteral("#ffffff"),
        .text = QStringLiteral("#1e293b"),
        .muted = QStringLiteral("#64748b"),
        .border = QStringLiteral("#d8e1eb"),
        .accent = QStringLiteral("#c8372e"),
        .accentSoft = QStringLiteral("#b45309"),
        .primary = QStringLiteral("#2563eb"),
        .primaryHover = QStringLiteral("#1d4ed8"),
        .primaryText = QStringLiteral("#ffffff"),
        .neutral = QStringLiteral("#f8fafc"),
        .neutralHover = QStringLiteral("#eef2f7"),
        .disabledSurface = QStringLiteral("#eef2f7"),
        .disabledText = QStringLiteral("#94a3b8"),
        .disabledBorder = QStringLiteral("#dbe3ec"),
        .danger = QStringLiteral("#fef2f2"),
        .dangerHover = QStringLiteral("#fee2e2"),
        .dangerBorder = QStringLiteral("#f3c9c4"),
        .dangerText = QStringLiteral("#c8372e"),
        .errorPane = QStringLiteral("#fff7ed"),
        .logLine = QStringLiteral("#fee2e2"),
        .logLineText = QStringLiteral("#991b1b"),
        .errorPrefix = QStringLiteral("#dc2626"),
        .progressTrack = QStringLiteral("#e2e8f0"),
    };

    return palette;
}

inline const Palette& DarkPalette()
{
    static const Palette palette{
        .background = QStringLiteral("#0f172a"),
        .surface = QStringLiteral("#1e293b"),
        .text = QStringLiteral("#e2e8f0"),
        .muted = QStringLiteral("#94a3b8"),
        .border = QStringLiteral("#334155"),
        .accent = QStringLiteral("#f87171"),
        .accentSoft = QStringLiteral("#fbbf24"),
        .primary = QStringLiteral("#2563eb"),
        .primaryHover = QStringLiteral("#1d4ed8"),
        .primaryText = QStringLiteral("#ffffff"),
        .neutral = QStringLiteral("#1e293b"),
        .neutralHover = QStringLiteral("#27364b"),
        .disabledSurface = QStringLiteral("#243044"),
        .disabledText = QStringLiteral("#64748b"),
        .disabledBorder = QStringLiteral("#334155"),
        .danger = QStringLiteral("#3b1c1f"),
        .dangerHover = QStringLiteral("#4a2327"),
        .dangerBorder = QStringLiteral("#7f2b2b"),
        .dangerText = QStringLiteral("#fca5a5"),
        .errorPane = QStringLiteral("#2b2117"),
        .logLine = QStringLiteral("#4c1d24"),
        .logLineText = QStringLiteral("#fecaca"),
        .errorPrefix = QStringLiteral("#f87171"),
        .progressTrack = QStringLiteral("#334155"),
    };

    return palette;
}

// The selected theme. Inline so this header stays header-only; the UI lives in a
// single process, as any QWidget-based application does.
inline ThemeMode g_themeMode = ThemeMode::Light;

inline const Palette& Colors()
{
    return g_themeMode == ThemeMode::Dark ? DarkPalette() : LightPalette();
}

inline ThemeMode CurrentThemeMode()
{
    return g_themeMode;
}

inline void SetThemeMode(ThemeMode mode)
{
    g_themeMode = mode;
}

inline QString ThemeModeName(ThemeMode mode)
{
    return mode == ThemeMode::Dark ? QStringLiteral("dark") : QStringLiteral("light");
}

inline ThemeMode ThemeModeFromName(const QString& name)
{
    return name.compare(QStringLiteral("dark"), Qt::CaseInsensitive) == 0
        ? ThemeMode::Dark
        : ThemeMode::Light;
}

// Qt palette that matches the theme. Most of the window is styled by hand, but the
// parts Qt draws itself - menu bar, table headers, scroll bars, dialogs - follow
// this one.
inline QPalette ApplicationPalette()
{
    const Palette& colors = Colors();

    QPalette palette;

    palette.setColor(QPalette::Window, ToColor(colors.background));
    palette.setColor(QPalette::WindowText, ToColor(colors.text));
    palette.setColor(QPalette::Base, ToColor(colors.surface));
    palette.setColor(QPalette::AlternateBase, ToColor(colors.background));
    palette.setColor(QPalette::ToolTipBase, ToColor(colors.surface));
    palette.setColor(QPalette::ToolTipText, ToColor(colors.text));
    palette.setColor(QPalette::Text, ToColor(colors.text));
    palette.setColor(QPalette::Button, ToColor(colors.neutral));
    palette.setColor(QPalette::ButtonText, ToColor(colors.text));
    palette.setColor(QPalette::BrightText, ToColor(colors.accent));
    palette.setColor(QPalette::Link, ToColor(colors.primary));
    palette.setColor(QPalette::Highlight, ToColor(colors.primary));
    palette.setColor(QPalette::HighlightedText, ToColor(colors.primaryText));
    palette.setColor(QPalette::PlaceholderText, ToColor(colors.muted));

    // Frame and separator shades.
    palette.setColor(QPalette::Light, ToColor(colors.border));
    palette.setColor(QPalette::Midlight, ToColor(colors.border));
    palette.setColor(QPalette::Mid, ToColor(colors.border));
    palette.setColor(QPalette::Dark, ToColor(colors.border));
    palette.setColor(QPalette::Shadow, ToColor(colors.border));

    // Greyed-out text stays readable on both backgrounds.
    palette.setColor(QPalette::Disabled, QPalette::Text, ToColor(colors.disabledText));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, ToColor(colors.disabledText));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, ToColor(colors.disabledText));

    return palette;
}

// The menu bar and its drop-down menus. Qt draws these itself, but the native
// Windows styles (windowsvista/windows11) paint them with the system theme and
// ignore the application palette, which leaves the dark theme with pale text on
// a pale background. Styling them by hand is what makes the top bar readable on
// Windows; only colours and the hover highlight are set, so the item spacing
// still comes from the platform style. The rules for the drop-downs travel with
// the menu bar, since every QMenu opened from it is its child widget.
inline QString MenuBarStyle()
{
    const Palette& colors = Colors();

    return QStringLiteral(
        "QMenuBar { background:%1; color:%2; border-bottom:1px solid %3; }"
        "QMenuBar::item { background:transparent; color:%2; }"
        "QMenuBar::item:selected { background:%4; color:%5; }"
        "QMenuBar::item:pressed { background:%6; color:%5; }")
        .arg(
            colors.background,
            colors.text,
            colors.border,
            colors.primary,
            colors.primaryText,
            colors.primaryHover);
}

// A drop-down menu, see MenuBarStyle() for why it is styled by hand.
inline QString MenuStyle()
{
    const Palette& colors = Colors();

    return QStringLiteral(
        "QMenu { background:%1; color:%2; border:1px solid %3; }"
        "QMenu::item { background:transparent; color:%2; }"
        "QMenu::item:selected { background:%4; color:%5; }"
        "QMenu::item:disabled { color:%6; }"
        "QMenu::separator { height:1px; background:%3; margin:4px 8px; }")
        .arg(
            colors.surface,
            colors.text,
            colors.border,
            colors.primary,
            colors.primaryText,
            colors.disabledText);
}

// Plain button, used for secondary actions.
inline QString NeutralButtonStyle()
{
    const Palette& colors = Colors();

    return QStringLiteral(
        "QPushButton { background:%1; color:%2; border:1px solid %3; "
        "border-radius:8px; padding:0 16px; }"
        "QPushButton:hover { background:%4; }"
        "QPushButton:disabled { background:%5; color:%6; border:1px solid %7; }")
        .arg(
            colors.neutral,
            colors.text,
            colors.border,
            colors.neutralHover,
            colors.disabledSurface,
            colors.disabledText,
            colors.disabledBorder);
}

// Highlighted button, used for the action the user is expected to take.
inline QString PrimaryButtonStyle()
{
    const Palette& colors = Colors();

    return QStringLiteral(
        "QPushButton { background:%1; color:%2; border:1px solid %1; "
        "border-radius:8px; padding:0 16px; }"
        "QPushButton:hover { background:%3; }"
        "QPushButton:disabled { background:%4; color:%5; border:1px solid %6; }")
        .arg(
            colors.primary,
            colors.primaryText,
            colors.primaryHover,
            colors.disabledSurface,
            colors.disabledText,
            colors.disabledBorder);
}

// Destructive button, used for actions that delete files. It is tinted like the
// error colour so it is not mistaken for a harmless one.
inline QString DangerButtonStyle()
{
    const Palette& colors = Colors();

    return QStringLiteral(
        "QPushButton { background:%1; color:%2; border:1px solid %3; "
        "border-radius:8px; padding:0 16px; }"
        "QPushButton:hover { background:%4; }"
        "QPushButton:disabled { background:%5; color:%6; border:1px solid %7; }")
        .arg(
            colors.danger,
            colors.dangerText,
            colors.dangerBorder,
            colors.dangerHover,
            colors.disabledSurface,
            colors.disabledText,
            colors.disabledBorder);
}

} // namespace ui

#pragma once

#include <QString>

// Colors and button styles shared by the log viewer and the mods panel, so the
// two parts of the window look the same without a stylesheet file.
namespace ui {

inline constexpr const char* kMutedColor = "#64748b";
inline constexpr const char* kTextColor = "#1e293b";
inline constexpr const char* kSurfaceColor = "#ffffff";
inline constexpr const char* kBorderColor = "#d8e1eb";
inline constexpr const char* kBackgroundColor = "#f4f7fb";
inline constexpr const char* kAccentColor = "#c8372e";
inline constexpr const char* kPrimaryColor = "#2563eb";
inline constexpr const char* kDisabledSurfaceColor = "#eef2f7";
inline constexpr const char* kDisabledTextColor = "#94a3b8";

// Plain button, used for secondary actions.
inline QString NeutralButtonStyle()
{
    return QStringLiteral(
        "QPushButton { background:#f8fafc; color:%1; border:1px solid %2; "
        "border-radius:8px; padding:0 16px; }"
        "QPushButton:hover { background:#eef2f7; }"
        "QPushButton:disabled { background:%3; color:%4; border:1px solid #dbe3ec; }")
        .arg(kTextColor, kBorderColor, kDisabledSurfaceColor, kDisabledTextColor);
}

// Highlighted button, used for the action the user is expected to take.
inline QString PrimaryButtonStyle()
{
    return QStringLiteral(
        "QPushButton { background:%1; color:#ffffff; border:1px solid %1; "
        "border-radius:8px; padding:0 16px; }"
        "QPushButton:hover { background:#1d4ed8; }"
        "QPushButton:disabled { background:%2; color:%3; border:1px solid #dbe3ec; }")
        .arg(kPrimaryColor, kDisabledSurfaceColor, kDisabledTextColor);
}

} // namespace ui

#pragma once

#include <QCoreApplication>
#include <QString>

#include <filesystem>
#include <string>

#include "core/PathUtils.h"

namespace mods {

// Diagnostics raised by this module. They are worded so the UI can show them
// directly after a translated prefix, and share one translation context.
inline QString Tr(const char* text)
{
    return QCoreApplication::translate("mods", text);
}

// QString -> std::filesystem::path. UTF-8 is used as the intermediate form
// because core::Utf8ToPath() turns it into UTF-16 on Windows and keeps the
// bytes untouched elsewhere.
inline std::filesystem::path ToPath(const QString& value)
{
    return core::Utf8ToPath(value.toUtf8().toStdString());
}

// std::filesystem::path -> QString (the inverse of ToPath()).
inline QString FromPath(const std::filesystem::path& value)
{
    const std::u8string utf8 = value.u8string();

    return QString::fromUtf8(
        reinterpret_cast<const char*>(utf8.data()),
        static_cast<int>(utf8.size()));
}

// Diagnostic strings produced by the core layer.
inline QString FromWide(const std::wstring& value)
{
    return QString::fromStdWString(value);
}

} // namespace mods

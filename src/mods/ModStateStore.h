#pragma once

#include <QString>

#include <optional>

#include "core/ModManifest.h"

namespace mods {

// Reads and writes "<mod directory>/.clv-mod-state.json", which records the
// version that is actually present on disk. Keeping it next to the mod files
// (rather than only in QSettings) means the answer stays correct when the user
// switches between several game installations or copies the folder around.
class ModStateStore
{
public:
    static QString StateFileName();

    // Returns an empty value when the file is missing or unreadable, which is
    // treated as "version unknown" by the caller.
    static std::optional<core::InstalledModState> Read(const QString& modDirectory);

    static bool Write(
        const QString& modDirectory,
        const core::InstalledModState& state,
        QString& error);

    static void Remove(const QString& modDirectory);

    // ISO-8601 timestamp for "installedAt".
    static QString NowIso8601();
};

} // namespace mods

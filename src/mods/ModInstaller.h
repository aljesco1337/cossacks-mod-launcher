#pragma once

#include <QString>

#include <functional>

#include "core/ModList.h"
#include "core/ModManifest.h"

namespace mods {

// Verifies and unpacks a downloaded mod archive into the game directory.
//
// The archive is extracted into a staging folder first and swapped into place
// afterwards, so an interrupted or cancelled install never leaves a half
// written mod behind. All filesystem work happens on the calling thread.
class ModInstaller
{
public:
    struct Result
    {
        bool success = false;
        QString error;    // empty on success
        QString warning;  // e.g. the version could not be recorded, empty otherwise
    };

    // "options" decides which folders besides this one may be switched on in the
    // game's mod list; the installed folder itself always is. The default switches
    // nothing extra on, which is the safe choice for a caller that has no list.
    //
    // "isCancelled" is polled while extracting and may be empty.
    static Result Install(
        const QString& gameDirectory,
        const core::ModRelease& release,
        const QString& archivePath,
        const core::ModListOptions& options = {},
        const std::function<bool()>& isCancelled = {});

    // Restores a folder that a previous run left between the two rename steps
    // and drops staging leftovers. Only call this while no install is running.
    static void Recover(const QString& gameDirectory, const core::ModRelease& release);

    // Absolute folder the mod occupies, e.g. "<gameDir>/mods/Renaissance".
    static QString ModDirectory(const QString& gameDirectory, const core::ModRelease& release);
};

} // namespace mods

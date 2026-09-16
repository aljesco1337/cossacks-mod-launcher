#include "ModList.h"

#include <algorithm>
#include <vector>

#include "ModsIni.h"
#include "PathUtils.h"
#include "Workshop.h"

namespace core {

namespace {

namespace fs = std::filesystem;

// A "dir" value: relative to the GAME folder, not to the "mods" folder that
// holds mods.ini. The game's own workshop records read
// "..\..\workshop\content\333420\<id>", which only resolves from
// "<library>/steamapps/common/Cossacks 3", so a folder inside the game is
// written as "mods\<name>".
//
// Textual on purpose: a manifest hint can install outside the game folder, in
// which case the record escapes it with ".." just like those workshop entries.
std::string GameRelativeDir(const fs::path& gameDirectory, const fs::path& folder)
{
    fs::path relative = folder.lexically_relative(gameDirectory);

    if (relative.empty() || relative == fs::path(L"."))
    {
        relative = folder.filename();
    }

    std::string value = PathToUtf8(relative);

    // The format uses Windows separators.
    std::replace(value.begin(), value.end(), '/', '\\');

    return value;
}

// The last component of a normalized "dir" value.
std::string NormalizedLeaf(const std::string& dir)
{
    const std::string normalized = NormalizeModsIniDir(dir);
    const std::size_t slash = normalized.rfind('/');

    return slash == std::string::npos ? normalized : normalized.substr(slash + 1);
}

// "value" names the record "dir" when it matches either completely or as its last
// component (the workshop id, or the folder name).
bool MatchesMod(const std::string& value, const std::string& dir)
{
    const std::string normalized = NormalizeModsIniDir(value);

    if (normalized.empty())
    {
        return false;
    }

    return SameModsIniDir(value, dir) || NormalizedLeaf(dir) == normalized;
}

// A record is compatible when the manifest names it, either by its complete "dir"
// value or by its last component (the workshop id, or the folder name).
bool IsCompatible(const ModListOptions& options, const std::string& dir)
{
    for (const std::string& compatible : options.compatibleMods)
    {
        if (MatchesMod(compatible, dir))
        {
            return true;
        }
    }

    return false;
}

// "modDirectory" may be empty, which lists the workshop items only.
bool EnsureListed(
    const fs::path& gameDirectory,
    const fs::path& modDirectory,
    const ModListOptions& options,
    bool& changed,
    std::string& error)
{
    changed = false;

    std::vector<ModsIniRecordState> states;

    // Workshop items first, so our own record stays the last one appended - the
    // same place it ends up in a list that already exists.
    for (const std::string& workshopDir : EnumerateWorkshopModDirs(gameDirectory))
    {
        ModsIniState state = ModsIniState::Disabled;

        if (MatchesMod(options.installedModWorkshopId, workshopDir))
        {
            // The mod itself, downloaded from the workshop: it is the
            // installation, so it is switched on.
            state = ModsIniState::Enabled;
        }
        else if (IsCompatible(options, workshopDir))
        {
            // Known to work with it: the user's own choice is kept.
            state = ModsIniState::Keep;
        }

        states.push_back(ModsIniRecordState{ workshopDir, state });
    }

    if (!modDirectory.empty())
    {
        // Switched on: the user asked for this mod by installing it.
        states.push_back(ModsIniRecordState{
            GameRelativeDir(gameDirectory, modDirectory), ModsIniState::Enabled });
    }

    if (states.empty())
    {
        // Nothing to list: leave a missing file missing rather than creating an
        // empty skeleton the game would have to cope with.
        return true;
    }

    bool applied = false;

    if (!EnsureModsIniStates(gameDirectory / L"mods", states, applied, error))
    {
        return false;
    }

    changed = applied;
    return true;
}

} // namespace

bool ListWorkshopMods(
    const std::filesystem::path& gameDirectory,
    const ModListOptions& options,
    bool& changed,
    std::string& error)
{
    return EnsureListed(gameDirectory, fs::path{}, options, changed, error);
}

bool ListInstalledMod(
    const std::filesystem::path& gameDirectory,
    const std::filesystem::path& modDirectory,
    const ModListOptions& options,
    bool& changed,
    std::string& error)
{
    return EnsureListed(gameDirectory, modDirectory, options, changed, error);
}

} // namespace core

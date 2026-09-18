#include "ModList.h"

#include <algorithm>
#include <cctype>
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

// The name the "Manage mods" dialog shows for a record. Unlike NormalizedLeaf()
// the spelling is kept: the user recognizes "Renaissance", not "renaissance".
std::string DisplayName(const std::string& dir)
{
    const std::size_t last = dir.find_last_not_of(" \t\r\n\\/");

    if (last == std::string::npos)
    {
        return {};
    }

    const std::string trimmed = dir.substr(0, last + 1);
    const std::size_t slash = trimmed.find_last_of("\\/");

    return slash == std::string::npos ? trimmed : trimmed.substr(slash + 1);
}

// A "dir" value resolved against the game folder. The value uses Windows
// separators and may climb out of the game folder (a workshop record reads
// "..\..\workshop\content\<appId>\<id>"), which std::filesystem does not
// understand as it stands, so the separators are normalized first.
fs::path ResolveGameRelativeDir(const fs::path& gameDirectory, const std::string& dir)
{
    std::string text = dir;

    std::replace(text.begin(), text.end(), '\\', '/');

    return (gameDirectory / Utf8ToPath(text)).lexically_normal();
}

// The name to show for a mod: the record's own "title" when the list has one, else
// the "title" of the mod folder's manifest, else the last component of "dir" -
// which for a workshop item is the far less useful numeric id.
std::string ModDisplayName(
    const fs::path& gameDirectory,
    const std::string& dir,
    const std::string& recordTitle)
{
    if (!recordTitle.empty())
    {
        return recordTitle;
    }

    std::string title;
    std::string error;

    if (ReadModTitle(ResolveGameRelativeDir(gameDirectory, dir), title, error) && !title.empty())
    {
        return title;
    }

    return DisplayName(dir);
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

std::string ToLowerAscii(const std::string& value)
{
    std::string lowered = value;

    std::transform(
        lowered.begin(),
        lowered.end(),
        lowered.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });

    return lowered;
}

// True when the list already holds a record for "dir".
bool ContainsDir(const std::vector<ModListEntry>& entries, const std::string& dir)
{
    for (const ModListEntry& entry : entries)
    {
        if (SameModsIniDir(entry.dir, dir))
        {
            return true;
        }
    }

    return false;
}

// Sub-folders of "<game folder>/mods", written the way mods.ini refers to them
// ("mods\<name>") and sorted by name. A folder no record mentions is a mod the game
// does not load yet.
std::vector<std::string> EnumerateLocalModDirs(const fs::path& gameDirectory)
{
    std::vector<std::string> dirs;

    std::error_code ec;

    for (fs::directory_iterator it(gameDirectory / L"mods", ec), end; !ec && it != end; it.increment(ec))
    {
        // A separate error_code: touching the loop's own one would stop it.
        std::error_code entryError;

        if (!it->is_directory(entryError))
        {
            // "mods.ini" and anything else that is not a mod folder.
            continue;
        }

        dirs.push_back(GameRelativeDir(gameDirectory, it->path()));
    }

    std::sort(dirs.begin(), dirs.end());

    return dirs;
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

bool CollectMods(
    const std::filesystem::path& gameDirectory,
    std::vector<ModListEntry>& entries,
    std::string& error)
{
    entries.clear();
    error.clear();

    ModsIniDocument document;
    bool exists = false;

    if (!ReadModsIni(gameDirectory / L"mods", document, exists, error))
    {
        return false;
    }

    for (const ModsIniEntry& entry : document.entries)
    {
        entries.push_back(ModListEntry{
            entry.dir,
            ModDisplayName(gameDirectory, entry.dir, entry.title),
            true,
            entry.enabled });
    }

    // Mods that are on disk but not in the list. They are switched off until the
    // user asks for them, and sorted by name so the dialog stays readable.
    std::vector<ModListEntry> found;

    const auto consider = [&](const std::string& dir)
    {
        if (ContainsDir(entries, dir) || ContainsDir(found, dir))
        {
            return;
        }

        found.push_back(ModListEntry{
            dir,
            ModDisplayName(gameDirectory, dir, std::string{}),
            false,
            false });
    };

    for (const std::string& dir : EnumerateLocalModDirs(gameDirectory))
    {
        consider(dir);
    }

    for (const std::string& dir : EnumerateWorkshopModDirs(gameDirectory))
    {
        consider(dir);
    }

    std::stable_sort(
        found.begin(),
        found.end(),
        [](const ModListEntry& left, const ModListEntry& right)
        {
            return ToLowerAscii(left.name) < ToLowerAscii(right.name);
        });

    entries.insert(entries.end(), found.begin(), found.end());

    return true;
}

bool SetModEnabled(
    const std::filesystem::path& gameDirectory,
    const std::string& dir,
    bool enabled,
    bool& changed,
    std::string& error)
{
    changed = false;
    error.clear();

    if (NormalizeModsIniDir(dir).empty())
    {
        error = "the mod directory is empty";
        return false;
    }

    if (!enabled)
    {
        // Switching off a mod the list does not mention: there is nothing to write,
        // and appending a disabled record would only make the file longer.
        ModsIniDocument document;
        bool exists = false;

        if (!ReadModsIni(gameDirectory / L"mods", document, exists, error))
        {
            return false;
        }

        bool listed = false;

        for (const ModsIniEntry& entry : document.entries)
        {
            if (SameModsIniDir(entry.dir, dir))
            {
                listed = true;
                break;
            }
        }

        if (!listed)
        {
            return true;
        }
    }

    const std::vector<ModsIniRecordState> states{
        ModsIniRecordState{ dir, enabled ? ModsIniState::Enabled : ModsIniState::Disabled },
    };

    return EnsureModsIniStates(gameDirectory / L"mods", states, changed, error);
}

bool MoveMod(
    const std::filesystem::path& gameDirectory,
    const std::string& dir,
    bool moveUp,
    bool& changed,
    std::string& error)
{
    changed = false;
    error.clear();

    if (NormalizeModsIniDir(dir).empty())
    {
        error = "the mod directory is empty";
        return false;
    }

    // The dialog shows one list and that list is the order: the records of mods.ini
    // in file order, followed by the mods that are only found on disk. A move swaps
    // two rows of it, and every row it touches ends up with a record so the file can
    // hold the order the user sees.
    std::vector<ModListEntry> entries;

    if (!CollectMods(gameDirectory, entries, error))
    {
        return false;
    }

    const int count = static_cast<int>(entries.size());
    int index = -1;

    for (int position = 0; position < count; position++)
    {
        if (SameModsIniDir(entries[position].dir, dir))
        {
            index = position;
            break;
        }
    }

    if (index < 0)
    {
        // Not a mod of this game folder: nothing to move.
        return true;
    }

    const int neighbour = moveUp ? index - 1 : index + 1;

    if (neighbour < 0 || neighbour >= count)
    {
        // First or last row: there is no neighbour to trade places with.
        return true;
    }

    const ModListEntry& moved = entries[index];
    const ModListEntry& other = entries[neighbour];

    if (moved.listed != other.listed)
    {
        // One of the two has no record yet. It is listed beside the other one -
        // switched off, so being given a place does not make the game load it - and
        // that is exactly the place the move asked for.
        const bool movedIsMissing = !moved.listed;
        const std::string& missing = movedIsMissing ? moved.dir : other.dir;
        const std::string& anchor = movedIsMissing ? other.dir : moved.dir;
        const bool missingFirst = movedIsMissing ? moveUp : !moveUp;

        return EnsureModsIniOrder(
            gameDirectory / L"mods", missing, anchor, missingFirst, changed, error);
    }

    if (moved.listed)
    {
        // Both have a record: they trade places.
        return EnsureModsIniOrder(
            gameDirectory / L"mods", moved.dir, other.dir, moveUp, changed, error);
    }

    // Neither has a record: the two are rows of the tail the file does not know in
    // any order, so the rows the move passes over have to be listed as well - the tail
    // keeps the order they are found in, not the one the user set, and would put them
    // back. They are listed switched off, in the order the list shows them.
    int firstTail = 0;

    while (firstTail < count && entries[firstTail].listed)
    {
        firstTail++;
    }

    std::vector<ModsIniRecordState> states;

    for (int position = firstTail; position <= std::max(index, neighbour); position++)
    {
        // The two rows that trade places, in the order the list will have them.
        int source = position;

        if (source == index)
        {
            source = neighbour;
        }
        else if (source == neighbour)
        {
            source = index;
        }

        states.push_back(ModsIniRecordState{ entries[source].dir, ModsIniState::Disabled });
    }

    return EnsureModsIniStates(gameDirectory / L"mods", states, changed, error);
}

} // namespace core

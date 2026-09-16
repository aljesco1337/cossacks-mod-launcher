#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace core {

// How the records written below are decided.
struct ModListOptions
{
    // Mods known to work together with the installed one. Their records are left
    // exactly as the user has them; an entry that is still missing is added
    // switched on.
    //
    // Entries match either a complete "dir" value ("mods\\Renaissance",
    // "..\\..\\workshop\\content\\333420\\3398700006") or just its last component
    // ("Renaissance", "3398700006"), compared case-insensitively.
    std::vector<std::string> compatibleMods;

    // Steam workshop id of the mod itself, matching the same way. When the mod is
    // downloaded from the workshop, that record is the installation, so it is
    // switched on instead of being treated like any other workshop item. Empty
    // when the mod only ever lives in the game's "mods" folder.
    std::string installedModWorkshopId;
};

// Keeps "<game folder>/mods/mods.ini" in sync with the folders that are
// available next to the game.
//
// Every function here only ever appends missing records: an existing record is
// never modified or removed, so the state the user set in game is preserved. The
// file is written at most once per call, and not at all when there is nothing to
// add.

// Lists every Steam workshop item found in the library the game is installed in.
// A record is switched on only when "options" allows it. Used when the game
// provides the mod itself, so that a list which was lost can be rebuilt without
// installing anything.
//
// Does nothing when the game has no workshop folder.
bool ListWorkshopMods(
    const std::filesystem::path& gameDirectory,
    const ModListOptions& options,
    bool& changed,
    std::string& error);

// Lists the workshop items as above and additionally registers "modDirectory"
// (the folder that was just installed). That record is always switched on: the
// user asked for it by installing the mod.
bool ListInstalledMod(
    const std::filesystem::path& gameDirectory,
    const std::filesystem::path& modDirectory,
    const ModListOptions& options,
    bool& changed,
    std::string& error);

} // namespace core

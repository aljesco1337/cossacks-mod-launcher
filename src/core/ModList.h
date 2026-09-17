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

// One mod the user can switch on or off, as the "Manage mods" dialog shows it.
struct ModListEntry
{
    // Path exactly as mods.ini writes it, i.e. relative to the game folder. It is
    // also the value used to switch the record.
    std::string dir;

    // Last component of "dir", used as the display name: "Renaissance" for a mod
    // folder, the workshop id for a workshop item.
    std::string name;

    // Whether mods.ini already has a record for this mod. A mod that is only found
    // on disk is switched off, and gets its record when the user switches it on.
    bool listed = false;

    // Whether the game loads it, i.e. whether the record's "dis" flag reads False.
    // A mod without a record counts as switched off.
    bool enabled = false;
};

// Everything the user can choose between: the records of mods.ini in the order the
// file has them, followed by the mod folders inside the game and the workshop items
// the file does not mention yet, sorted by name.
//
// Returns false - with an empty list - when mods.ini exists but cannot be read or
// parsed (a UTF-16 file, for instance); a list that is not there yet is fine.
bool CollectMods(
    const std::filesystem::path& gameDirectory,
    std::vector<ModListEntry>& entries,
    std::string& error);

// Switches one mod on or off in "<game folder>/mods/mods.ini".
//
// Switching a mod off that the file does not mention yet does nothing: without a
// record the game does not load it either, and appending a disabled record would
// only make the list longer. "changed" reports whether the file was written.
bool SetModEnabled(
    const std::filesystem::path& gameDirectory,
    const std::string& dir,
    bool enabled,
    bool& changed,
    std::string& error);

} // namespace core

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace core {

// Parser for the game's mod list, "<game folder>/mods/mods.ini". The file uses
// the engine's "struct" markup:
//
//   section.begin
//      mods : struct.begin
//         [*] : struct.begin
//            dir = ..\..\workshop\content\333420\3398700006
//            dis = True
//         struct.end
//      struct.end
//   section.end
//
// All text handled here is UTF-8 (see core/PathUtils.h); the file is edited and
// written back byte for byte, so unknown sections and exotic characters survive.
struct ModsIniEntry
{
    // Path exactly as written in the file. It is relative to the GAME folder,
    // not to the "mods" folder that holds mods.ini: "mods\Renaissance" for a
    // folder inside the game, "..\..\workshop\content\333420\<id>" for a
    // workshop item, which resolves against "<library>/steamapps".
    std::string dir;

    // Whether the game should load this folder. The file stores the opposite
    // under the key "dis" (disabled), so an enabled record reads
    // "dis = False". An entry without the key counts as disabled, because it
    // never opted in.
    bool enabled = false;
};

struct ModsIniDocument
{
    std::vector<ModsIniEntry> entries;
};

// A record to make sure the file lists, in the form the file uses.
struct ModsIniRecord
{
    std::string dir;       // relative to the game folder
    bool enabled = false;  // written inverted, see ModsIniFlagFor()
};

inline constexpr const char* kModsIniFileName = "mods.ini";

// The list stores the inverse of what the flag means: the key is called "dis"
// (disabled), so switching a mod OFF is written as "dis = True" and switching it
// ON as "dis = False". These constants name the text rather than the meaning,
// which keeps the inversion in one place.
inline constexpr const char* kModsIniDisabledFlag = "True";
inline constexpr const char* kModsIniEnabledFlag = "False";

// The "dis" value to write for an enabled/disabled record.
inline const char* ModsIniFlagFor(bool enabled)
{
    return enabled ? kModsIniEnabledFlag : kModsIniDisabledFlag;
}

// Parses the mod list. Constructs this launcher does not know are skipped, so a
// file with more sections keeps working. Fails only on a structurally broken
// file (an unbalanced "mods" struct).
bool ParseModsIni(const std::string& text, ModsIniDocument& document, std::string& error);

// Canonical form used to compare two "dir" values: separators, "." and ".."
// segments and letter case do not matter, because the game resolves them all to
// the same folder.
std::string NormalizeModsIniDir(const std::string& dir);

bool SameModsIniDir(const std::string& left, const std::string& right);

// Appends an element for "gameRelativeDir" to the "mods" struct. Existing
// content, including blocks this launcher does not understand, is preserved
// verbatim.
//
// "updatedText" receives the new content, or stays empty when the entry is
// already listed (nothing to do). A text without a "mods" struct produces a
// complete skeleton instead of failing.
bool AddModsIniEntry(
    const std::string& text,
    const std::string& gameRelativeDir,
    bool enabled,
    std::string& updatedText,
    std::string& error);

// Appends every record that is not listed yet, in one pass. "updatedText"
// stays empty when nothing had to be added.
bool AddModsIniRecords(
    const std::string& text,
    const std::vector<ModsIniRecord>& records,
    std::string& updatedText,
    std::string& error);

// Reads "<modsFolder>/mods.ini", appends the records that are missing and
// writes the file back once. Nothing is written when everything is already
// listed. "added" reports whether the file was modified.
bool EnsureModsIniRecords(
    const std::filesystem::path& modsFolder,
    const std::vector<ModsIniRecord>& records,
    bool& added,
    std::string& error);

// Convenience wrapper for a single record.
bool EnsureModsIniEntry(
    const std::filesystem::path& modsFolder,
    const std::string& gameRelativeDir,
    bool enabled,
    bool& added,
    std::string& error);

} // namespace core

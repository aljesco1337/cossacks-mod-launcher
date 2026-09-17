#pragma once

#include <string>

namespace core {

// The game's configuration file, "<game folder>/cossacks.ini". Like the mod
// list it uses the engine's "struct" markup:
//
//   section.begin
//      GameSaveDirectoryPath=cossacks
//      LogFileEnabled = true
//      LogFileRoot = true
//      LogFileName = cos
//      HideHelloScreen = true
//   section.end
//
// Only the two logging switches are read and rewritten here. The text is edited
// in place, so comments, indentation, spacing, the line ending style and a
// UTF-8 BOM survive byte for byte; UTF-16 files are refused.
inline constexpr const char* kCossacksIniFileName = "cossacks.ini";

// The engine writes its log files only when both switches are on.
inline constexpr const char* kLogEnabledKey = "LogFileEnabled";
inline constexpr const char* kLogRootKey = "LogFileRoot";

struct LogIniSettings
{
    // True when the file already carries both switches.
    bool present = false;

    bool enabled = false;  // LogFileEnabled
    bool root = false;     // LogFileRoot
};

// Reads the logging switches. A missing key leaves the matching field false, so
// this cannot fail; use LogIniSettings::present to tell "off" from "absent".
LogIniSettings ReadLogSettings(const std::string& text);

// Turns both switches on or off. Keys that are already there are rewritten in
// place; missing ones are added next to their sibling (or, when the file has
// neither, into the section holding the engine's own settings).
//
// Fails - leaving the file untouched - for UTF-16 input.
bool SetLogSettings(
    const std::string& text,
    bool enabled,
    std::string& updated,
    std::string& error
);

} // namespace core

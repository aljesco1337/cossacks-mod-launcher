#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace core {

// Steam application id of Cossacks 3.
inline constexpr const char* kCossacksSteamAppId = "333420";

// Folder that holds the workshop items of the game:
// "<library>/steamapps/workshop/content/<appId>", which the game folder reaches
// as "..\..\workshop\content\<appId>" (two levels up from
// "<library>/steamapps/common/<game>" is "steamapps").
//
// Empty when that folder does not exist: a GOG installation, a game folder
// outside a Steam library, or a library where nothing was downloaded.
std::filesystem::path WorkshopContentFolder(const std::filesystem::path& gameDirectory);

// Folders found inside the workshop content folder, written the way mods.ini
// expects them (relative to the game folder, Windows separators) and sorted by
// name. Empty when there is no workshop folder. Not recursive.
std::vector<std::string> EnumerateWorkshopModDirs(const std::filesystem::path& gameDirectory);

} // namespace core

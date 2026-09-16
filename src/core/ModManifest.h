#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace core {

// One mod entry of the distribution manifest (see the "distribution" branch,
// manifest.json). Only the fields the launcher consumes are modelled; unknown
// JSON members are ignored by the parser.
//
// All text fields hold UTF-8. Display strings are handed to Qt with
// QString::fromUtf8(), paths are converted with core::Utf8ToPath().
struct ModRelease
{
    std::string id;              // manifest map key, e.g. "ren"
    std::string workshopId;      // Steam published file id, e.g. "3398700006"
    std::string name;            // display name, e.g. "Renaissance"
    std::string versionLabel;    // display version, e.g. "0.33"
    int versionNumber = 0;       // comparable version, e.g. 330
    std::string updatedAt;       // ISO-8601 timestamp from the manifest
    std::string downloadUrl;
    std::string sha256;          // lowercase hex, empty when not published
    long long size = 0;          // expected archive size in bytes, 0 = unknown

    // Optional install hints. Absent from the first schema revision, in which
    // case the launcher derives the destination from "name" and strips the
    // archive's single top level folder when it has one.
    std::string installDir;      // relative to the game directory
    std::string archiveRoot;     // leading path component to drop on extract
};

struct ModManifest
{
    int schemaVersion = 0;
    std::vector<ModRelease> mods; // sorted by id for stable iteration
};

// State of the mod as it is installed in the game directory. Persisted as
// "<installDir>/.clv-mod-state.json" so it travels with the mod files.
struct InstalledModState
{
    int schemaVersion = 1;
    std::string id;
    int versionNumber = 0;
    std::string versionLabel;
    std::string sha256;
    std::string installedAt;
    std::string downloadUrl;
};

enum class ModUpdateStatus
{
    Unknown,          // the manifest does not offer a usable version
    NotInstalled,     // nothing installed for this mod id
    UpToDate,         // the installed version matches or is ahead of it
    UpdateAvailable,  // the manifest offers a newer version
};

// Returns true when "candidateVersionNumber" is a newer release than the one
// that is installed. Version labels are never compared as text: "0.9" would
// sort above "0.33" as a string.
bool IsNewer(int candidateVersionNumber, int installedVersionNumber);

ModUpdateStatus CompareVersions(const ModRelease& release, const InstalledModState& installed);

// Replaces characters that are invalid in a file name and falls back to the
// mod id when nothing usable is left.
std::string SanitizeFolderName(const std::string& name, const std::string& fallbackId);

// Absolute directory the mod is installed into: "<gameDir>/<installDir>" when
// the manifest provides one, otherwise "<gameDir>/mods/<sanitized name>".
std::filesystem::path ResolveInstallDir(
    const std::filesystem::path& gameDirectory,
    const ModRelease& release);

} // namespace core

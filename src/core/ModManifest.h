#pragma once

#include <filesystem>
#include <optional>
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

// The launcher's own release, as published in the manifest's "app" member.
//
// The launcher never downloads an update by itself: a newer version is announced
// and the release page is handed to the browser. That keeps the app out of the
// business of replacing its own running binaries, which on Windows is locked and
// in an AppImage is a read-only mount anyway.
struct AppRelease
{
    std::string versionLabel;   // what the manifest publishes, e.g. "0.2.0"
    std::string releasePageUrl; // release page to open in the browser

    // Derived from versionLabel by the parser, never read from the manifest: the
    // mod entries carry a hand-written number, but the app's own version has to
    // agree with the label the user sees, so it is computed from it.
    int versionNumber = 0;
};

struct ModManifest
{
    int schemaVersion = 0;
    std::vector<ModRelease> mods; // sorted by id for stable iteration

    // Mods that are known to work together with this one, e.g. ["3123019560"].
    //
    // During an installation every mod the launcher lists is written switched off
    // except the installed mod itself and these, see core/ModList.h. Optional:
    // a manifest without the member simply has no compatible mods.
    //
    // Entries match a folder name or a workshop id, see core/ModList.h.
    std::vector<std::string> compatibleMods;

    // The launcher's own release. Optional: a manifest without the member, or
    // with an unusable one, simply means "no app release is known".
    std::optional<AppRelease> app;
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

// Turns a dotted version label into the comparable number the manifest publishes
// for a mod: "0.2.0" -> 200, "1.4.2" -> 10402 (major * 10000 + minor * 100 +
// patch), so minor and patch have to stay below 100.
//
// The launcher's own release is compared the same way: its version comes from
// the release tag, the manifest publishes the same label, and both sides run
// through this function. That is what makes the tag the only source of a version
// and leaves the manifest without a number that could disagree with its label.
//
// A "v" prefix and anything after the third component are ignored ("v0.2.0-rc1"
// -> 200, "1.2.3.4" -> 10203); a label without a single digit returns 0, which
// never compares as an update.
int VersionNumberFromLabel(const std::string& label);

// Replaces characters that are invalid in a file name and falls back to the
// mod id when nothing usable is left.
std::string SanitizeFolderName(const std::string& name, const std::string& fallbackId);

// Absolute directory the mod is installed into: "<gameDir>/<installDir>" when
// the manifest provides one, otherwise "<gameDir>/mods/<sanitized name>".
std::filesystem::path ResolveInstallDir(
    const std::filesystem::path& gameDirectory,
    const ModRelease& release);

} // namespace core

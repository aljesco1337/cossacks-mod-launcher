#include "ModManifest.h"

#include "PathUtils.h"

namespace core {

namespace {

bool IsInvalidFileNameByte(unsigned char byte)
{
    if (byte < 32 || byte == 127)
    {
        return true;
    }

    switch (byte)
    {
    case '<':
    case '>':
    case ':':
    case '"':
    case '/':
    case '\\':
    case '|':
    case '?':
    case '*':
        return true;
    default:
        return false;
    }
}

} // namespace

bool IsNewer(int candidateVersionNumber, int installedVersionNumber)
{
    return candidateVersionNumber > installedVersionNumber;
}

ModUpdateStatus CompareVersions(const ModRelease& release, const InstalledModState& installed)
{
    if (release.versionNumber <= 0)
    {
        return ModUpdateStatus::Unknown;
    }

    if (installed.versionNumber <= 0)
    {
        return ModUpdateStatus::NotInstalled;
    }

    return IsNewer(release.versionNumber, installed.versionNumber)
        ? ModUpdateStatus::UpdateAvailable
        : ModUpdateStatus::UpToDate;
}

std::string SanitizeFolderName(const std::string& name, const std::string& fallbackId)
{
    // Sanitizing byte-wise is safe for UTF-8: every byte of a multi byte
    // sequence has the high bit set, so it can never collide with the ASCII
    // characters that are rejected here.
    std::string sanitized;
    sanitized.reserve(name.size());

    for (const char raw : name)
    {
        const unsigned char byte = static_cast<unsigned char>(raw);
        sanitized.push_back(IsInvalidFileNameByte(byte) ? '_' : raw);
    }

    // Windows silently drops trailing dots and spaces, which would make the
    // folder name recorded in the state file differ from the one on disk.
    while (!sanitized.empty() && (sanitized.back() == '.' || sanitized.back() == ' '))
    {
        sanitized.pop_back();
    }

    if (sanitized == "." || sanitized == "..")
    {
        sanitized.clear();
    }

    if (sanitized.empty())
    {
        sanitized = fallbackId;
    }

    return sanitized;
}

std::filesystem::path ResolveInstallDir(
    const std::filesystem::path& gameDirectory,
    const ModRelease& release)
{
    if (!release.installDir.empty())
    {
        return gameDirectory / Utf8ToPath(release.installDir);
    }

    return gameDirectory / L"mods" / Utf8ToPath(SanitizeFolderName(release.name, release.id));
}

} // namespace core

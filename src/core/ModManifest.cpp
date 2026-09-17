#include "ModManifest.h"

#include <cctype>

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

int VersionNumberFromLabel(const std::string& label)
{
    // major * 10000 + minor * 100 + patch. Reading the label instead of taking
    // a second number from the build is what keeps the release tag the single
    // source of a version.
    constexpr int kComponentCount = 3;
    constexpr int kComponentLimit = 999;

    int components[kComponentCount] = { 0, 0, 0 };
    int index = 0;
    bool sawDigit = false;

    for (const char raw : label)
    {
        const unsigned char character = static_cast<unsigned char>(raw);

        if (std::isdigit(character) != 0)
        {
            sawDigit = true;

            // Clamped rather than overflowed: a nonsensical label must not wrap
            // around into a version that compares as older (or newer).
            if (components[index] < kComponentLimit)
            {
                components[index] = components[index] * 10 + (character - '0');
            }

            continue;
        }

        if (character == '.')
        {
            if (!sawDigit)
            {
                continue;
            }

            if (index + 1 >= kComponentCount)
            {
                // A fourth component ("1.2.3.4") ends the version. Its digits
                // must not run into the patch number.
                break;
            }

            ++index;
            continue;
        }

        // Text before the first digit is skipped ("v0.2.0"), text after it ends
        // the version ("0.2.0-rc1" and "0.2.0 (beta)" both stop at 200).
        if (sawDigit)
        {
            break;
        }
    }

    if (!sawDigit)
    {
        return 0;
    }

    return components[0] * 10000 + components[1] * 100 + components[2];
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

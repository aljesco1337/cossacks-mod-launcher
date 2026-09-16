#include "Workshop.h"

#include <algorithm>
#include <system_error>

#include "PathUtils.h"

namespace core {

namespace {

namespace fs = std::filesystem;

// "..\..\workshop\content\<appId>" seen from the game folder. Purely textual, so
// it also works for a folder that does not exist.
fs::path WorkshopContentCandidate(const fs::path& gameDirectory)
{
    return (gameDirectory / ".." / ".." / "workshop" / "content" / kCossacksSteamAppId)
        .lexically_normal();
}

} // namespace

std::filesystem::path WorkshopContentFolder(const std::filesystem::path& gameDirectory)
{
    if (gameDirectory.empty())
    {
        return {};
    }

    const fs::path candidate = WorkshopContentCandidate(gameDirectory);

    std::error_code ec;

    return fs::is_directory(candidate, ec) ? candidate : fs::path{};
}

std::vector<std::string> EnumerateWorkshopModDirs(const std::filesystem::path& gameDirectory)
{
    std::vector<std::string> dirs;

    const fs::path contentFolder = WorkshopContentFolder(gameDirectory);

    if (contentFolder.empty())
    {
        return dirs;
    }

    std::error_code ec;

    for (fs::directory_iterator it(contentFolder, ec), end; !ec && it != end; it.increment(ec))
    {
        // A separate error_code: touching the loop's own one would stop it.
        std::error_code entryError;

        if (!it->is_directory(entryError))
        {
            continue;
        }

        // Derived from the game folder on purpose: the game resolves these
        // relative paths the same way, so the record matches what it writes.
        const fs::path relative = it->path().lexically_relative(gameDirectory);

        if (relative.empty())
        {
            continue;
        }

        std::string value = PathToUtf8(relative);

        // The format uses Windows separators.
        std::replace(value.begin(), value.end(), '/', '\\');

        dirs.push_back(std::move(value));
    }

    std::sort(dirs.begin(), dirs.end());

    return dirs;
}

} // namespace core

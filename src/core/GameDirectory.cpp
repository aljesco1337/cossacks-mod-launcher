#include "GameDirectory.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>

namespace core {

namespace fs = std::filesystem;

namespace {

// Case-insensitive lookup of a file/directory name inside a directory. Needed
// because game installs differ in the casing of the executable name.
bool HasEntryNamed(const fs::path& directory, const std::wstring& expectedName)
{
    std::error_code ec;

    if (!fs::is_directory(directory, ec))
    {
        return false;
    }

    std::wstring lowered;
    lowered.reserve(expectedName.size());
    for (wchar_t ch : expectedName)
    {
        lowered.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }

    for (fs::directory_iterator it(directory, ec), end;
         !ec && it != end;
         it.increment(ec))
    {
        std::wstring name = it->path().filename().wstring();
        for (wchar_t& ch : name)
        {
            ch = static_cast<wchar_t>(std::towlower(ch));
        }

        if (name == lowered)
        {
            return true;
        }
    }

    return false;
}

} // namespace

bool IsValidGameDirectory(const std::wstring& path)
{
    if (path.empty())
    {
        return false;
    }

    const fs::path gameDir(path);

    if (!fs::is_regular_file(gameDir / L"data" / L"scripts" / L"dmscript.global"))
    {
        return false;
    }

    return HasEntryNamed(gameDir, L"cossacks.exe") ||
        HasEntryNamed(gameDir, L"cossacks");
}

std::wstring NormalizeGameDirectory(const std::wstring& input)
{
    if (input.empty())
    {
        return {};
    }

    std::wstring path = input;

    std::error_code ec;
    const fs::path parsed(path);
    const fs::path canonical = fs::weakly_canonical(parsed, ec);

    path = (!ec && !canonical.empty())
        ? canonical.wstring()
        : parsed.lexically_normal().wstring();

#ifdef _WIN32
    std::replace(path.begin(), path.end(), L'/', L'\\');

    if (path.size() >= 2 && path[1] == L':')
    {
        path[0] = static_cast<wchar_t>(std::towupper(path[0]));
    }
#endif

    return path;
}

} // namespace core

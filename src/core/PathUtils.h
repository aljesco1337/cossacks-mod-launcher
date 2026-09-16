#pragma once

#include <filesystem>
#include <string>

namespace core {

// Interprets UTF-8 bytes as a filesystem path. On Windows this performs the
// UTF-8 -> UTF-16 conversion the Win32 APIs expect; elsewhere the bytes are
// kept as they are. Used for every string that crosses the manifest (JSON,
// always UTF-8) into the filesystem.
inline std::filesystem::path Utf8ToPath(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(utf8.data()),
        utf8.size()));
}

// std::filesystem::path -> UTF-8, the inverse of Utf8ToPath(). Always uses '/'
// as the separator, whichever platform this runs on.
inline std::string PathToUtf8(const std::filesystem::path& value)
{
    const std::u8string utf8 = value.generic_u8string();

    return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
}

} // namespace core

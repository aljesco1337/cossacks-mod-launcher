#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace core {

// Inspects the archive's table of contents and reports its single top level
// folder ("Renaissance" for an archive laid out as "Renaissance/..."). The
// result stays empty when the archive has entries at its root, or several
// different roots.
bool FindZipRootFolder(
    const std::filesystem::path& archivePath,
    std::filesystem::path& rootFolder,
    std::wstring& error);

// Extracts every entry of "archivePath" below "destination". A non-empty
// "stripPrefix" removes that leading path component from every entry, which is
// how a release archive that wraps its content in a single folder is flattened.
//
// Entry paths are validated before use: absolute paths, drive letters and ".."
// components are rejected so a malicious archive cannot write outside
// "destination". "isCancelled" is polled between entries and may be empty.
bool ExtractZip(
    const std::filesystem::path& archivePath,
    const std::filesystem::path& destination,
    const std::filesystem::path& stripPrefix,
    const std::function<bool()>& isCancelled,
    std::wstring& error);

} // namespace core

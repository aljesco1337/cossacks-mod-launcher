#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Types.h"

namespace core {

// Enumerates log files inside "<gameDir>/log". The result is unsorted.
std::vector<LogFileInfo> EnumerateLogFiles(const std::wstring& gameDir);

// Sorts log files by last-write time. When descending is true the newest file
// comes first; ties are broken by file name.
void SortLogFiles(std::vector<LogFileInfo>& files, bool descending);

// Formats a file time as "YYYY-MM-DD HH:MM:SS" in local time.
std::wstring FormatModifiedTime(const std::filesystem::file_time_type& time);

} // namespace core

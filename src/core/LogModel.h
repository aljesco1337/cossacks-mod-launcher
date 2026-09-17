#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Types.h"

namespace core {

// Enumerates log files inside "<gameDir>/log". The result is unsorted.
std::vector<LogFileInfo> EnumerateLogFiles(const std::wstring& gameDir);

// What DeleteLogFiles() managed to remove.
struct LogDeletionResult
{
    int deleted = 0;

    // Files that are still there, typically because the running game holds them
    // open. A locked file does not stop the others from being deleted.
    std::vector<std::wstring> failed;
};

// Deletes every regular file inside "<gameDir>/log" - the files the log list
// shows; subfolders are left untouched.
//
// Returns false only when the operation could not start (no game folder, or no
// "log" directory in it), so the caller can tell "nothing was there to delete"
// from "deleting went wrong".
bool DeleteLogFiles(const std::wstring& gameDir, LogDeletionResult& result, std::wstring& error);

// Sorts log files by last-write time. When descending is true the newest file
// comes first; ties are broken by file name.
void SortLogFiles(std::vector<LogFileInfo>& files, bool descending);

// Formats a file time as "YYYY-MM-DD HH:MM:SS" in local time.
std::wstring FormatModifiedTime(const std::filesystem::file_time_type& time);

} // namespace core

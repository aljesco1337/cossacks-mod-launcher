#include "LogModel.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <cwchar>

namespace core {

std::vector<LogFileInfo> EnumerateLogFiles(const std::wstring& gameDir)
{
    std::vector<LogFileInfo> files;

    if (gameDir.empty())
    {
        return files;
    }

    std::error_code ec;
    const fs::path logsDirectory = fs::path(gameDir) / L"log";

    if (!fs::is_directory(logsDirectory, ec))
    {
        return files;
    }

    for (fs::directory_iterator it(logsDirectory, ec), end;
         !ec && it != end;
         it.increment(ec))
    {
        if (!it->is_regular_file(ec))
        {
            continue;
        }

        LogFileInfo info;
        info.fileName = it->path().filename().wstring();
        info.fullPath = it->path().wstring();
        info.modifiedTime = it->last_write_time(ec);
        files.push_back(std::move(info));
    }

    return files;
}

bool DeleteLogFiles(const std::wstring& gameDir, LogDeletionResult& result, std::wstring& error)
{
    result = LogDeletionResult{};

    if (gameDir.empty())
    {
        error = L"No game folder is selected.";
        return false;
    }

    std::error_code ec;
    const fs::path logsDirectory = fs::path(gameDir) / L"log";

    if (!fs::is_directory(logsDirectory, ec))
    {
        error = L"The game folder has no \"log\" directory.";
        return false;
    }

    for (fs::directory_iterator it(logsDirectory, ec), end;
         !ec && it != end;
         it.increment(ec))
    {
        std::error_code fileError;
        const fs::path path = it->path();

        // Subfolders stay untouched: the log list shows the files next to them,
        // and those are the ones the user asked to delete.
        if (!it->is_regular_file(fileError))
        {
            continue;
        }

        if (fs::remove(path, fileError) && !fileError)
        {
            result.deleted++;
        }
        else
        {
            result.failed.push_back(path.filename().wstring());
        }
    }

    // The iteration itself failed, so the folder could not be read to the end.
    // Whatever was deleted before that stays deleted, which "result" reports.
    if (ec)
    {
        error = L"The log folder could not be read completely.";
        return false;
    }

    return true;
}

void SortLogFiles(std::vector<LogFileInfo>& files, bool descending)
{
    std::sort(
        files.begin(),
        files.end(),
        [descending](const LogFileInfo& left, const LogFileInfo& right)
        {
            if (left.modifiedTime == right.modifiedTime)
            {
                return left.fileName < right.fileName;
            }

            return descending
                ? left.modifiedTime > right.modifiedTime
                : left.modifiedTime < right.modifiedTime;
        }
    );
}

std::wstring FormatModifiedTime(const fs::file_time_type& time)
{
    using namespace std::chrono;

    // Portable conversion to system time. The filesystem clock has an
    // implementation-defined epoch, so translate through the offset between
    // the two clocks rather than relying on a non-portable to_sys member.
    const auto systemTime = time_point_cast<system_clock::duration>(
        time - fs::file_time_type::clock::now() + system_clock::now()
    );
    const std::time_t value = system_clock::to_time_t(systemTime);

    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &value);
#else
    localtime_r(&value, &local);
#endif

    wchar_t buffer[64]{};
    std::swprintf(
        buffer,
        64,
        L"%04d-%02d-%02d %02d:%02d:%02d",
        local.tm_year + 1900,
        local.tm_mon + 1,
        local.tm_mday,
        local.tm_hour,
        local.tm_min,
        local.tm_sec
    );

    return buffer;
}

} // namespace core

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

    const auto systemTime = file_clock::to_sys(time);
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

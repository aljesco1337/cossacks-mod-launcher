#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace core {

namespace fs = std::filesystem;

struct LogFileInfo
{
    std::wstring fileName;
    std::wstring fullPath;
    fs::file_time_type modifiedTime;
};

struct LogSeverityCounts
{
    int errors = 0;
    int critical = 0;
};

struct FrameworkCodeLine
{
    int globalLine = 0;
    int frameworkPhysicalLine = 0;
    std::wstring content;
};

struct FrameworkDefinition
{
    std::vector<FrameworkCodeLine> codeLines;
    std::vector<std::wstring> filePaths;
};

struct SourceFileRange
{
    std::wstring relativePath;
    fs::path absolutePath;
    int firstGlobalLine = 0;
    int lastGlobalLine = -1;
    std::vector<std::wstring> lines;
};

struct ScriptLineMap
{
    fs::path frameworkPath;
    std::vector<FrameworkCodeLine> codeLines;
    std::vector<SourceFileRange> sourceFiles;
};

struct ParsedScriptError
{
    int globalLine = 0;
    int column = 0;
    std::wstring message;
};

} // namespace core

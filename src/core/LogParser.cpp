#include "LogParser.h"

#include <algorithm>
#include <regex>
#include <sstream>

#include "GameDirectory.h"
#include "TextUtils.h"

namespace core {

std::optional<std::wstring> FindFrameworkPath(const fs::path& globalScriptPath, std::wstring& error)
{
    if (!fs::is_regular_file(globalScriptPath))
    {
        error = L"Global script does not exist:\r\n" + globalScriptPath.wstring();
        return std::nullopt;
    }

    static const std::wregex frameworkPattern(
        LR"regex(^\s*framework\s*=\s*(.+?)\s*$)regex",
        std::regex_constants::icase
    );

    std::vector<std::wstring> lines = SplitLinesPreserveTrailing(ReadTextFile(globalScriptPath.wstring()));
    for (const std::wstring& line : lines)
    {
        std::wsmatch match;
        if (std::regex_match(line, match, frameworkPattern))
        {
            return CleanConfigValue(match[1].str());
        }
    }

    error = L"Cannot find 'framework = ...' in:\r\n" + globalScriptPath.wstring();
    return std::nullopt;
}

std::optional<FrameworkDefinition> ReadFrameworkDefinition(const fs::path& frameworkPath, std::wstring& error)
{
    if (!fs::is_regular_file(frameworkPath))
    {
        error = L"Framework file does not exist:\r\n" + frameworkPath.wstring();
        return std::nullopt;
    }

    static const std::wregex codeSectionPattern(
        LR"regex(^\s*Code\s*:\s*struct\.begin\s*$)regex",
        std::regex_constants::icase
    );
    static const std::wregex filesSectionPattern(
        LR"regex(^\s*Files\s*:\s*struct\.begin\s*$)regex",
        std::regex_constants::icase
    );
    static const std::wregex structEndPattern(
        LR"regex(^\s*struct\.end\s*$)regex",
        std::regex_constants::icase
    );
    static const std::wregex codeLinePattern(
        LR"regex(^\s*\[\*\]\s*=\s*;(.*)$)regex"
    );
    static const std::wregex fileLinePattern(
        LR"regex(^\s*\[\*\]\s*=\s*(.+?)\s*$)regex"
    );

    enum class Section
    {
        None,
        Code,
        Files
    };

    FrameworkDefinition framework;
    Section section = Section::None;
    std::vector<std::wstring> lines = SplitLinesPreserveTrailing(ReadTextFile(frameworkPath.wstring()));

    for (size_t index = 0; index < lines.size(); index++)
    {
        const std::wstring& line = lines[index];

        if (std::regex_match(line, codeSectionPattern))
        {
            section = Section::Code;
            continue;
        }

        if (std::regex_match(line, filesSectionPattern))
        {
            section = Section::Files;
            continue;
        }

        if (section != Section::None && std::regex_match(line, structEndPattern))
        {
            section = Section::None;
            continue;
        }

        std::wsmatch match;
        if (section == Section::Code && std::regex_match(line, match, codeLinePattern))
        {
            FrameworkCodeLine codeLine;
            codeLine.globalLine = static_cast<int>(framework.codeLines.size());
            codeLine.frameworkPhysicalLine = static_cast<int>(index + 1);
            codeLine.content = match[1].str();
            framework.codeLines.push_back(std::move(codeLine));
        }
        else if (section == Section::Files && std::regex_match(line, match, fileLinePattern))
        {
            std::wstring path = CleanConfigValue(match[1].str());
            if (!path.empty())
            {
                framework.filePaths.push_back(std::move(path));
            }
        }
    }

    if (framework.codeLines.empty())
    {
        error = L"The framework Code section contains no '[*] = ;...' lines:\r\n" + frameworkPath.wstring();
        return std::nullopt;
    }

    if (framework.filePaths.empty())
    {
        error = L"The framework Files section contains no file paths:\r\n" + frameworkPath.wstring();
        return std::nullopt;
    }

    return framework;
}

fs::path ResolveGamePath(const fs::path& gameDir, const std::wstring& configuredPath)
{
    std::wstring value = CleanConfigValue(configuredPath);
    std::replace(value.begin(), value.end(), L'/', L'\\');

    while (value.starts_with(L".\\"))
    {
        value.erase(0, 2);
    }

    while (!value.empty() && (value.front() == L'\\' || value.front() == L'/'))
    {
        value.erase(value.begin());
    }

    fs::path resolved = gameDir / value;
    std::error_code error;
    fs::path canonical = fs::weakly_canonical(resolved, error);

    return !error && !canonical.empty()
        ? canonical
        : resolved.lexically_normal();
}

std::optional<ScriptLineMap> BuildScriptLineMap(
    const fs::path& gameDir,
    const fs::path& frameworkPath,
    const FrameworkDefinition& framework,
    std::wstring& error
)
{
    constexpr int beforeFirstSourceFileLines = 2;
    constexpr int betweenSourceFilesLines = 1;

    ScriptLineMap lineMap;
    lineMap.frameworkPath = frameworkPath;
    lineMap.codeLines = framework.codeLines;

    int nextGlobalLine = static_cast<int>(framework.codeLines.size()) + beforeFirstSourceFileLines;

    for (const std::wstring& relativePath : framework.filePaths)
    {
        fs::path absolutePath = ResolveGamePath(gameDir, relativePath);

        if (!fs::is_regular_file(absolutePath))
        {
            error = L"Source file does not exist:\r\n" + absolutePath.wstring();
            return std::nullopt;
        }

        std::vector<std::wstring> lines = SplitLinesPreserveTrailing(ReadTextFile(absolutePath.wstring()));

        SourceFileRange range;
        range.relativePath = relativePath;
        range.absolutePath = absolutePath;
        range.firstGlobalLine = nextGlobalLine;
        range.lastGlobalLine = lines.empty()
            ? nextGlobalLine - 1
            : nextGlobalLine + static_cast<int>(lines.size()) - 1;
        range.lines = std::move(lines);

        nextGlobalLine += static_cast<int>(range.lines.size()) + betweenSourceFilesLines;
        lineMap.sourceFiles.push_back(std::move(range));
    }

    return lineMap;
}

std::optional<ParsedScriptError> ParseScriptError(const std::wstring& text, std::wstring& error)
{
    static const std::wregex errorPattern(
        LR"regex(Line:\s*(\d+)\s*,\s*Column:\s*(\d+)\s*:\s*(.+?)\s*$)regex",
        std::regex_constants::icase
    );

    std::wsmatch match;
    if (!std::regex_search(text, match, errorPattern))
    {
        error = L"Cannot extract line and column from selected error.";
        return std::nullopt;
    }

    ParsedScriptError parsed;
    parsed.globalLine = std::stoi(match[1].str());
    parsed.column = std::stoi(match[2].str());
    parsed.message = match[3].str();
    return parsed;
}

std::wstring ResolveScriptErrorText(const std::wstring& gameDirectory, const std::wstring& errorLine)
{
    std::wstring error;
    auto parsedError = ParseScriptError(errorLine, error);
    if (!parsedError)
    {
        return L"SOURCE LOCATION UNAVAILABLE\r\n\r\n" + error;
    }

    if (parsedError->globalLine < 0)
    {
        return L"SOURCE LOCATION UNAVAILABLE\r\n\r\nGlobal line cannot be negative.";
    }

    std::wstring normalizedGameDirectory = NormalizeGameDirectory(gameDirectory);
    if (normalizedGameDirectory.empty())
    {
        return L"SOURCE LOCATION UNAVAILABLE\r\n\r\nBase directory is empty.";
    }

    fs::path gameDir(normalizedGameDirectory);
    fs::path globalScriptPath = gameDir / L"data" / L"scripts" / L"dmscript.global";

    auto frameworkValue = FindFrameworkPath(globalScriptPath, error);
    if (!frameworkValue)
    {
        return L"SOURCE LOCATION UNAVAILABLE\r\n\r\n" + error;
    }

    fs::path frameworkPath = ResolveGamePath(gameDir, *frameworkValue);
    auto framework = ReadFrameworkDefinition(frameworkPath, error);
    if (!framework)
    {
        return L"SOURCE LOCATION UNAVAILABLE\r\n\r\n" + error;
    }

    auto lineMap = BuildScriptLineMap(gameDir, frameworkPath, *framework, error);
    if (!lineMap)
    {
        return L"SOURCE LOCATION UNAVAILABLE\r\n\r\n" + error;
    }

    std::wstringstream output;
    output << L"RESOLVED SOURCE LOCATION\r\n\r\n";
    output << L"Global line: " << parsedError->globalLine << L"\r\n";
    output << L"Column: " << parsedError->column << L"\r\n";
    output << L"Message: " << parsedError->message << L"\r\n\r\n";

    auto appendContent = [&output](const std::wstring& content, int column)
    {
        std::wstring expandedContent = ExpandTabs(content, 4);
        size_t rawPrefixLength = static_cast<size_t>(
            std::max(0, std::min(column - 1, static_cast<int>(content.size())))
        );
        std::wstring expandedPrefix = ExpandTabs(content.substr(0, rawPrefixLength), 4);

        output << expandedContent << L"\r\n";
        output << std::wstring(expandedPrefix.size(), L' ') << L"^";
    };

    if (parsedError->globalLine < static_cast<int>(lineMap->codeLines.size()))
    {
        const FrameworkCodeLine& codeLine = lineMap->codeLines[static_cast<size_t>(parsedError->globalLine)];

        output << L"Location: framework Code section\r\n";
        output << L"File: " << lineMap->frameworkPath.wstring() << L"\r\n";
        output << L"Framework physical line: " << codeLine.frameworkPhysicalLine << L"\r\n\r\n";
        appendContent(codeLine.content, parsedError->column);
        return output.str();
    }

    for (size_t index = 0; index < lineMap->sourceFiles.size(); index++)
    {
        const SourceFileRange& file = lineMap->sourceFiles[index];

        if (parsedError->globalLine >= file.firstGlobalLine &&
            parsedError->globalLine <= file.lastGlobalLine)
        {
            int zeroBasedSourceLine = parsedError->globalLine - file.firstGlobalLine;
            int oneBasedSourceLine = zeroBasedSourceLine + 1;
            const std::wstring& content = file.lines[static_cast<size_t>(zeroBasedSourceLine)];

            output << L"File: " << file.absolutePath.wstring() << L"\r\n";
            output << L"Relative file: " << file.relativePath << L"\r\n";
            output << L"Source line: " << oneBasedSourceLine << L"\r\n";
            output << L"Source line index: " << zeroBasedSourceLine << L"\r\n\r\n";
            appendContent(content, parsedError->column);
            return output.str();
        }

        int separatorStart = index == 0
            ? static_cast<int>(lineMap->codeLines.size())
            : lineMap->sourceFiles[index - 1].lastGlobalLine + 1;
        int separatorEnd = file.firstGlobalLine - 1;

        if (parsedError->globalLine >= separatorStart &&
            parsedError->globalLine <= separatorEnd)
        {
            output.str(L"");
            output.clear();
            output << L"SOURCE LOCATION UNAVAILABLE\r\n\r\n";
            output << L"Global line " << parsedError->globalLine
                << L" is an engine-added separator line before "
                << file.relativePath << L".";
            return output.str();
        }
    }

    int lastKnownLine = lineMap->sourceFiles.empty()
        ? static_cast<int>(lineMap->codeLines.size()) - 1
        : lineMap->sourceFiles.back().lastGlobalLine;

    output.str(L"");
    output.clear();
    output << L"SOURCE LOCATION UNAVAILABLE\r\n\r\n";
    output << L"Global line " << parsedError->globalLine
        << L" is outside the generated script. Last known line is "
        << lastKnownLine << L".";
    return output.str();
}

} // namespace core

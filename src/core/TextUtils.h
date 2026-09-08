#pragma once

#include <string>
#include <vector>

#include "Types.h"

namespace core {

std::wstring Trim(std::wstring value);

std::wstring CleanConfigValue(const std::wstring& value);

std::wstring ExpandTabs(const std::wstring& text, int tabSize);

std::vector<std::wstring> SplitLinesPreserveTrailing(const std::wstring& text);

std::wstring NormalizeLineEndings(const std::wstring& text);

std::wstring FormatPreviewText(const std::wstring& text);

LogSeverityCounts CountLogSeverity(const std::wstring& text);

// Reads a file while auto-detecting its text encoding (UTF-8, UTF-16 LE/BE,
// Windows-1251). On failure, returns a short error description.
std::wstring ReadTextFile(const std::wstring& path);

} // namespace core

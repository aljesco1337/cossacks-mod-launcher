#pragma once

#include <filesystem>
#include <optional>
#include <string>

#include "Types.h"

namespace core {

// Finds the "framework = ..." value inside the global script file.
std::optional<std::wstring> FindFrameworkPath(const fs::path& globalScriptPath, std::wstring& error);

// Parses the Code and Files sections of a framework definition file.
std::optional<FrameworkDefinition> ReadFrameworkDefinition(const fs::path& frameworkPath, std::wstring& error);

// Resolves a configured (possibly relative) path against the game directory.
fs::path ResolveGamePath(const fs::path& gameDir, const std::wstring& configuredPath);

// Builds the global-line mapping from framework code lines + source files.
std::optional<ScriptLineMap> BuildScriptLineMap(
    const fs::path& gameDir,
    const fs::path& frameworkPath,
    const FrameworkDefinition& framework,
    std::wstring& error
);

// Extracts "Line: N, Column: N: message" from a compile error line.
std::optional<ParsedScriptError> ParseScriptError(const std::wstring& text, std::wstring& error);

// Resolves a compile-error line to a human readable description of the exact
// source location (framework line or a concrete source file line).
std::wstring ResolveScriptErrorText(const std::wstring& gameDirectory, const std::wstring& errorLine);

} // namespace core

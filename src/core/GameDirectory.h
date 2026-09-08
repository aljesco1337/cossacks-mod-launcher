#pragma once

#include <string>

namespace core {

// Returns true when the given path looks like a Cossacks 3 installation:
// it contains "data/scripts/dmscript.global" and a game executable.
bool IsValidGameDirectory(const std::wstring& path);

// Normalizes a game directory path (absolute, canonical form). On Windows the
// drive letter is upper-cased and separators are converted to backslashes.
std::wstring NormalizeGameDirectory(const std::wstring& input);

} // namespace core

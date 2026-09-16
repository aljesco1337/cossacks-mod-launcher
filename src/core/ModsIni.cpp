#include "ModsIni.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>

namespace core {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t kNotFound = std::string::npos;

enum class TokenKind
{
    Ignored,
    StructBegin,
    StructEnd,
    Value,
};

struct Token
{
    TokenKind kind = TokenKind::Ignored;
    std::string name;   // struct name for StructBegin ("mods", "[*]")
    std::string key;
    std::string value;
};

std::string Trim(const std::string& value)
{
    const std::size_t first = value.find_first_not_of(" \t\r\n");

    if (first == kNotFound)
    {
        return {};
    }

    const std::size_t last = value.find_last_not_of(" \t\r\n");

    return value.substr(first, last - first + 1);
}

std::string ToLowerAscii(const std::string& value)
{
    std::string lowered = value;

    std::transform(
        lowered.begin(),
        lowered.end(),
        lowered.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });

    return lowered;
}

bool EqualsIgnoreCase(const std::string& left, const std::string& right)
{
    return ToLowerAscii(left) == ToLowerAscii(right);
}

// Splits on every line ending and keeps a trailing empty element for text that
// ends with a newline, so joining again reproduces the input exactly.
std::vector<std::string> SplitLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::string current;

    for (std::size_t index = 0; index < text.size(); index++)
    {
        const char character = text[index];

        if (character == '\r')
        {
            lines.push_back(current);
            current.clear();

            if (index + 1 < text.size() && text[index + 1] == '\n')
            {
                index++;
            }
        }
        else if (character == '\n')
        {
            lines.push_back(current);
            current.clear();
        }
        else
        {
            current.push_back(character);
        }
    }

    lines.push_back(current);

    return lines;
}

std::string JoinLines(const std::vector<std::string>& lines, const std::string& lineEnding)
{
    std::string text;

    for (std::size_t index = 0; index < lines.size(); index++)
    {
        if (index > 0)
        {
            text += lineEnding;
        }

        text += lines[index];
    }

    return text;
}

// Keeps the line ending style of the file being edited.
std::string DetectLineEnding(const std::string& text)
{
    for (std::size_t index = 0; index < text.size(); index++)
    {
        if (text[index] == '\r')
        {
            return (index + 1 < text.size() && text[index + 1] == '\n')
                ? std::string("\r\n")
                : std::string("\r");
        }

        if (text[index] == '\n')
        {
            return "\n";
        }
    }

    // A file created from scratch follows the format's Windows origin.
    return "\r\n";
}

std::string IndentationOf(const std::string& line)
{
    const std::size_t first = line.find_first_not_of(" \t");

    return first == kNotFound ? std::string{} : line.substr(0, first);
}

Token Tokenize(const std::string& rawLine)
{
    Token token;
    const std::string line = Trim(rawLine);

    if (line.empty() || line.rfind("//", 0) == 0 || line.rfind(";", 0) == 0)
    {
        return token;
    }

    if (line == "struct.end")
    {
        token.kind = TokenKind::StructEnd;
        return token;
    }

    // Struct headers are written as "<name> : struct.begin"; array elements use
    // the literal name "[*]".
    const std::string structSuffix = ": struct.begin";
    const std::size_t structAt = line.rfind(structSuffix);

    if (structAt != kNotFound && structAt + structSuffix.size() == line.size())
    {
        token.kind = TokenKind::StructBegin;
        token.name = Trim(line.substr(0, structAt));
        return token;
    }

    const std::size_t equalsAt = line.find('=');

    if (equalsAt != kNotFound)
    {
        token.kind = TokenKind::Value;
        token.key = Trim(line.substr(0, equalsAt));
        token.value = Trim(line.substr(equalsAt + 1));
        return token;
    }

    // "section.begin" / "section.end" and anything else: not needed here.
    return token;
}

// Value of the "dis" key. It names "disabled", so it means the opposite of
// whether the game loads the mod. Nothing is returned for an unknown value, so a
// surprising file cannot switch a mod on by accident.
std::optional<bool> ParseFlagValue(const std::string& value)
{
    if (value.empty())
    {
        return std::nullopt;
    }

    if (EqualsIgnoreCase(value, "true") || value == "1" ||
        EqualsIgnoreCase(value, "yes") || EqualsIgnoreCase(value, "on"))
    {
        return true;
    }

    if (EqualsIgnoreCase(value, "false") || value == "0" ||
        EqualsIgnoreCase(value, "no") || EqualsIgnoreCase(value, "off"))
    {
        return false;
    }

    return std::nullopt;
}

// True when the innermost open struct is an element of the "mods" list.
bool InsideModsElement(const std::vector<std::string>& structStack)
{
    return structStack.size() >= 2 &&
        EqualsIgnoreCase(structStack[structStack.size() - 2], "mods") &&
        structStack.back() == "[*]";
}

std::string BuildSkeleton(const std::string& relativeDir, bool enabled, const std::string& lineEnding)
{
    const std::string flag = ModsIniFlagFor(enabled);

    const std::vector<std::string> lines = {
        "section.begin",
        "   mods : struct.begin",
        "      [*] : struct.begin",
        "         dir = " + relativeDir,
        "         dis = " + flag,
        "      struct.end",
        "   struct.end",
        "section.end",
    };

    return JoinLines(lines, lineEnding) + lineEnding;
}

bool ReadFileBytes(const fs::path& filePath, std::string& bytes, bool& exists, std::string& error)
{
    bytes.clear();
    exists = false;
    error.clear();

    std::error_code ec;

    if (!fs::exists(filePath, ec))
    {
        return true;
    }

    std::ifstream input(filePath, std::ios::binary);

    if (!input)
    {
        error = "the mod list could not be opened";
        return false;
    }

    bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());

    if (input.bad())
    {
        error = "the mod list could not be read";
        return false;
    }

    exists = true;
    return true;
}

} // namespace

std::string NormalizeModsIniDir(const std::string& dir)
{
    std::string input = Trim(dir);

    for (char& character : input)
    {
        if (character == '\\')
        {
            character = '/';
        }
    }

    // Resolved textually: the referenced folders may not exist yet (a workshop
    // item that is not downloaded, for instance), so the filesystem cannot be
    // asked for the real path.
    std::vector<std::string> parts;
    std::size_t start = 0;

    while (start <= input.size())
    {
        const std::size_t slash = input.find('/', start);
        const std::string part = input.substr(
            start,
            slash == kNotFound ? kNotFound : slash - start);

        if (!part.empty() && part != ".")
        {
            if (part == ".." && !parts.empty() && parts.back() != "..")
            {
                parts.pop_back();
            }
            else
            {
                parts.push_back(part);
            }
        }

        if (slash == kNotFound)
        {
            break;
        }

        start = slash + 1;
    }

    std::string normalized;

    for (const std::string& part : parts)
    {
        if (!normalized.empty())
        {
            normalized += '/';
        }

        normalized += part;
    }

    return ToLowerAscii(normalized);
}

bool SameModsIniDir(const std::string& left, const std::string& right)
{
    const std::string normalizedLeft = NormalizeModsIniDir(left);
    const std::string normalizedRight = NormalizeModsIniDir(right);

    return !normalizedLeft.empty() && normalizedLeft == normalizedRight;
}

bool ParseModsIni(const std::string& text, ModsIniDocument& document, std::string& error)
{
    document.entries.clear();
    error.clear();

    std::vector<std::string> structStack;
    std::size_t currentEntry = kNotFound;

    for (const std::string& line : SplitLines(text))
    {
        const Token token = Tokenize(line);

        switch (token.kind)
        {
        case TokenKind::StructBegin:
            structStack.push_back(token.name);

            if (InsideModsElement(structStack))
            {
                document.entries.push_back(ModsIniEntry{});
                currentEntry = document.entries.size() - 1;
            }
            break;

        case TokenKind::StructEnd:
            if (InsideModsElement(structStack))
            {
                currentEntry = kNotFound;
            }

            if (structStack.empty())
            {
                error = "the mod list has more \"struct.end\" lines than \"struct.begin\" lines";
                return false;
            }

            structStack.pop_back();
            break;

        case TokenKind::Value:
            if (currentEntry != kNotFound)
            {
                ModsIniEntry& entry = document.entries[currentEntry];

                if (EqualsIgnoreCase(token.key, "dir"))
                {
                    entry.dir = token.value;
                }
                else if (EqualsIgnoreCase(token.key, "dis"))
                {
                    // Only an explicit false switches the mod on.
                    if (const std::optional<bool> disabled = ParseFlagValue(token.value))
                    {
                        entry.enabled = !*disabled;
                    }
                }
            }
            break;

        case TokenKind::Ignored:
            break;
        }
    }

    if (!structStack.empty())
    {
        error = "the mod list has an unterminated \"struct.begin\" block";
        return false;
    }

    // Entries that never carried a "dir" line are useless to the game and to us.
    document.entries.erase(
        std::remove_if(
            document.entries.begin(),
            document.entries.end(),
            [](const ModsIniEntry& entry) { return entry.dir.empty(); }),
        document.entries.end());

    return true;
}

bool AddModsIniEntry(
    const std::string& text,
    const std::string& gameRelativeDir,
    bool enabled,
    std::string& updatedText,
    std::string& error)
{
    updatedText.clear();
    error.clear();

    const std::string dir = Trim(gameRelativeDir);

    if (dir.empty())
    {
        error = "the mod directory is empty";
        return false;
    }

    ModsIniDocument document;

    if (!ParseModsIni(text, document, error))
    {
        return false;
    }

    for (const ModsIniEntry& entry : document.entries)
    {
        if (SameModsIniDir(entry.dir, dir))
        {
            // Already listed: the existing record, including its enabled flag,
            // is left exactly as the user had it.
            return true;
        }
    }

    const std::string lineEnding = DetectLineEnding(text);

    std::vector<std::string> lines = SplitLines(text);

    std::size_t modsStart = kNotFound;
    std::size_t modsEnd = kNotFound;
    int depth = 0;

    for (std::size_t index = 0; index < lines.size(); index++)
    {
        const Token token = Tokenize(lines[index]);

        if (token.kind == TokenKind::StructBegin)
        {
            if (modsStart == kNotFound && EqualsIgnoreCase(token.name, "mods"))
            {
                modsStart = index;
                depth = 0;
            }

            if (modsStart != kNotFound)
            {
                depth++;
            }
        }
        else if (token.kind == TokenKind::StructEnd && modsStart != kNotFound)
        {
            depth--;

            if (depth == 0)
            {
                modsEnd = index;
                break;
            }
        }
    }

    if (modsStart == kNotFound)
    {
        // No list at all: build a complete file, keeping the line ending style
        // of whatever we were given (including nothing).
        updatedText = BuildSkeleton(dir, enabled, lineEnding);
        return true;
    }

    if (modsEnd == kNotFound)
    {
        error = "the \"mods\" block in the mod list is not terminated";
        return false;
    }

    // Match the indentation already in use, so hand edits stay tidy.
    std::string elementIndent = IndentationOf(lines[modsStart]) + "   ";

    for (std::size_t index = modsStart + 1; index < modsEnd; index++)
    {
        if (Tokenize(lines[index]).kind == TokenKind::StructBegin)
        {
            elementIndent = IndentationOf(lines[index]);
            break;
        }
    }

    const std::string keyIndent = elementIndent + "   ";
    const std::string flag = ModsIniFlagFor(enabled);

    const std::vector<std::string> block = {
        elementIndent + "[*] : struct.begin",
        keyIndent + "dir = " + dir,
        keyIndent + "dis = " + flag,
        elementIndent + "struct.end",
    };

    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(modsEnd), block.begin(), block.end());

    updatedText = JoinLines(lines, lineEnding);
    return true;
}

bool AddModsIniRecords(
    const std::string& text,
    const std::vector<ModsIniRecord>& records,
    std::string& updatedText,
    std::string& error)
{
    updatedText.clear();
    error.clear();

    // Each record is applied to the result of the previous one, so the caller
    // pays for a single file write no matter how many records there are.
    std::string current = text;
    bool changed = false;

    for (const ModsIniRecord& record : records)
    {
        std::string next;

        if (!AddModsIniEntry(current, record.dir, record.enabled, next, error))
        {
            return false;
        }

        if (!next.empty())
        {
            current = std::move(next);
            changed = true;
        }
    }

    if (changed)
    {
        updatedText = std::move(current);
    }

    return true;
}

bool EnsureModsIniRecords(
    const std::filesystem::path& modsFolder,
    const std::vector<ModsIniRecord>& records,
    bool& added,
    std::string& error)
{
    added = false;

    const fs::path filePath = modsFolder / kModsIniFileName;

    std::string bytes;
    bool exists = false;

    if (!ReadFileBytes(filePath, bytes, exists, error))
    {
        return false;
    }

    std::string bom;

    if (bytes.rfind("\xEF\xBB\xBF", 0) == 0)
    {
        bom = "\xEF\xBB\xBF";
        bytes.erase(0, 3);
    }
    else if (bytes.rfind("\xFF\xFE", 0) == 0 || bytes.rfind("\xFE\xFF", 0) == 0)
    {
        // Rewriting UTF-16 as UTF-8 would make the game's copy unreadable, and
        // re-encoding it is out of scope: report instead of corrupting.
        error = "the mod list is stored as UTF-16 and was left untouched";
        return false;
    }

    std::string updated;

    if (!AddModsIniRecords(bytes, records, updated, error))
    {
        return false;
    }

    if (updated.empty())
    {
        // Already listed.
        return true;
    }

    std::error_code ec;
    fs::create_directories(modsFolder, ec);

    if (ec)
    {
        error = "the mods folder could not be created";
        return false;
    }

    std::ofstream output(filePath, std::ios::binary | std::ios::trunc);

    if (!output)
    {
        error = "the mod list could not be written";
        return false;
    }

    output.write(bom.data(), static_cast<std::streamsize>(bom.size()));
    output.write(updated.data(), static_cast<std::streamsize>(updated.size()));
    output.close();

    if (!output)
    {
        error = "the mod list could not be written";
        return false;
    }

    added = true;
    return true;
}

bool EnsureModsIniEntry(
    const std::filesystem::path& modsFolder,
    const std::string& gameRelativeDir,
    bool enabled,
    bool& added,
    std::string& error)
{
    return EnsureModsIniRecords(
        modsFolder,
        { ModsIniRecord{ gameRelativeDir, enabled } },
        added,
        error);
}

} // namespace core

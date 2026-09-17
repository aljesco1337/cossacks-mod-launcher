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

// Where one element of the "mods" list sits in the file, so its "dis" value can be
// updated without touching anything else.
struct Element
{
    std::string dir;
    std::size_t beginLine = kNotFound;
    std::size_t endLine = kNotFound;
    std::size_t dirLine = kNotFound;
    std::size_t disLine = kNotFound;
};

bool FindElements(const std::vector<std::string>& lines, std::vector<Element>& elements, std::string& error)
{
    elements.clear();
    error.clear();

    std::vector<std::string> structStack;
    std::size_t current = kNotFound;

    for (std::size_t index = 0; index < lines.size(); index++)
    {
        const Token token = Tokenize(lines[index]);

        switch (token.kind)
        {
        case TokenKind::StructBegin:
            structStack.push_back(token.name);

            if (InsideModsElement(structStack))
            {
                elements.push_back(Element{});
                current = elements.size() - 1;
                elements[current].beginLine = index;
            }
            break;

        case TokenKind::StructEnd:
            if (InsideModsElement(structStack) && current != kNotFound)
            {
                elements[current].endLine = index;
                current = kNotFound;
            }

            if (structStack.empty())
            {
                error = "the mod list has more \"struct.end\" lines than \"struct.begin\" lines";
                return false;
            }

            structStack.pop_back();
            break;

        case TokenKind::Value:
            if (current != kNotFound)
            {
                if (EqualsIgnoreCase(token.key, "dir"))
                {
                    elements[current].dir = token.value;
                    elements[current].dirLine = index;
                }
                else if (EqualsIgnoreCase(token.key, "dis"))
                {
                    elements[current].disLine = index;
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

    return true;
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

// Reads the list and splits off a UTF-8 BOM, which has to be written back but must
// not take part in the parsing. A UTF-16 file is refused: rewriting it as UTF-8
// would make the game's copy unreadable, and re-encoding it is out of scope.
bool ReadListText(
    const fs::path& filePath,
    std::string& bom,
    std::string& text,
    bool& exists,
    std::string& error)
{
    bom.clear();
    text.clear();

    if (!ReadFileBytes(filePath, text, exists, error))
    {
        return false;
    }

    if (text.rfind("\xEF\xBB\xBF", 0) == 0)
    {
        bom = "\xEF\xBB\xBF";
        text.erase(0, 3);
    }
    else if (text.rfind("\xFF\xFE", 0) == 0 || text.rfind("\xFE\xFF", 0) == 0)
    {
        error = "the mod list is stored as UTF-16 and was left untouched";
        text.clear();
        return false;
    }

    return true;
}

// Writes the list back exactly as it was read: BOM first, then the text the editor
// produced. The folder is created when it is not there yet.
bool WriteListText(
    const fs::path& filePath,
    const std::string& bom,
    const std::string& text,
    std::string& error)
{
    std::error_code ec;
    fs::create_directories(filePath.parent_path(), ec);

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
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();

    if (!output)
    {
        error = "the mod list could not be written";
        return false;
    }

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
                else if (EqualsIgnoreCase(token.key, "title"))
                {
                    // The name the game shows; only used for display, the launcher
                    // never writes it.
                    entry.title = token.value;
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

bool ApplyModsIniStates(
    const std::string& text,
    const std::vector<ModsIniRecordState>& states,
    std::string& updatedText,
    std::string& error)
{
    updatedText.clear();
    error.clear();

    std::string current = text;
    bool changed = false;

    // 1. Append the records the file does not have yet. One that has to be created
    //    is written switched on unless it is meant to be off, so a compatible mod
    //    is usable without the user having to add it by hand.
    for (const ModsIniRecordState& state : states)
    {
        const std::string dir = Trim(state.dir);

        if (dir.empty())
        {
            continue;
        }

        ModsIniDocument document;

        if (!ParseModsIni(current, document, error))
        {
            return false;
        }

        bool known = false;

        for (const ModsIniEntry& entry : document.entries)
        {
            if (SameModsIniDir(entry.dir, dir))
            {
                known = true;
                break;
            }
        }

        if (known)
        {
            continue;
        }

        std::string next;

        if (!AddModsIniEntry(current, dir, state.state != ModsIniState::Disabled, next, error))
        {
            return false;
        }

        if (!next.empty())
        {
            current = std::move(next);
            changed = true;
        }
    }

    // 2. Update the records that are already there.
    std::vector<std::string> lines = SplitLines(current);
    std::vector<Element> elements;

    if (!FindElements(lines, elements, error))
    {
        return false;
    }

    struct Edit
    {
        std::size_t index = 0;
        std::vector<std::string> replacement;
    };

    std::vector<Edit> edits;

    for (const ModsIniRecordState& state : states)
    {
        if (state.state == ModsIniState::Keep)
        {
            continue;
        }

        const std::string flag = ModsIniFlagFor(state.state == ModsIniState::Enabled);

        for (const Element& element : elements)
        {
            if (!SameModsIniDir(element.dir, state.dir))
            {
                continue;
            }

            if (element.disLine == kNotFound)
            {
                // No flag at all yet: add one beside "dir".
                if (element.beginLine == kNotFound)
                {
                    break;
                }

                const std::size_t anchor = element.dirLine != kNotFound
                    ? element.dirLine
                    : element.beginLine;

                const std::string indent = element.dirLine != kNotFound
                    ? IndentationOf(lines[element.dirLine])
                    : IndentationOf(lines[element.beginLine]) + "   ";

                edits.push_back(Edit{ anchor, { lines[anchor], indent + "dis = " + flag } });
            }
            else
            {
                const std::string rebuilt =
                    IndentationOf(lines[element.disLine]) + "dis = " + flag;

                if (lines[element.disLine] != rebuilt)
                {
                    edits.push_back(Edit{ element.disLine, { rebuilt } });
                }
            }

            break;
        }
    }

    // Applied from the bottom, so the indices of the remaining edits stay valid.
    std::sort(edits.begin(), edits.end(), [](const Edit& left, const Edit& right)
    {
        return left.index > right.index;
    });

    for (const Edit& edit : edits)
    {
        lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(edit.index));
        lines.insert(
            lines.begin() + static_cast<std::ptrdiff_t>(edit.index),
            edit.replacement.begin(),
            edit.replacement.end());

        changed = true;
    }

    if (changed)
    {
        updatedText = JoinLines(lines, DetectLineEnding(current));
    }

    return true;
}

bool SwapModsIniRecords(
    const std::string& text,
    const std::string& dir,
    bool moveUp,
    std::string& updatedText,
    std::string& error)
{
    updatedText.clear();
    error.clear();

    if (NormalizeModsIniDir(dir).empty())
    {
        error = "the mod directory is empty";
        return false;
    }

    std::vector<std::string> lines = SplitLines(text);
    std::vector<Element> elements;

    if (!FindElements(lines, elements, error))
    {
        return false;
    }

    // The records in the order the file has them. An element without a "dir" line
    // is not a mod the caller can name, so it is not part of that order - and
    // because only the two records trade places, it keeps its own line where it is.
    std::vector<std::size_t> records;

    for (std::size_t index = 0; index < elements.size(); index++)
    {
        if (!NormalizeModsIniDir(elements[index].dir).empty())
        {
            records.push_back(index);
        }
    }

    std::size_t position = kNotFound;

    for (std::size_t index = 0; index < records.size(); index++)
    {
        if (SameModsIniDir(elements[records[index]].dir, dir))
        {
            position = index;
            break;
        }
    }

    // A record the list does not have has no place to move to, and one at the top
    // or the bottom is already as far as it goes.
    if (position == kNotFound ||
        (moveUp ? position == 0 : position + 1 == records.size()))
    {
        return true;
    }

    const Element& moving = elements[records[position]];
    const Element& neighbour = elements[records[moveUp ? position - 1 : position + 1]];

    const auto blockAt = [&lines](const Element& element)
    {
        return std::vector<std::string>(
            lines.begin() + static_cast<std::ptrdiff_t>(element.beginLine),
            lines.begin() + static_cast<std::ptrdiff_t>(element.endLine) + 1);
    };

    const std::vector<std::string> movingBlock = blockAt(moving);
    const std::vector<std::string> neighbourBlock = blockAt(neighbour);

    // Whatever sits between the two records - blank lines, comments - is left in
    // place, so the records trade places around it.
    const std::size_t gapFirst = std::min(moving.beginLine, neighbour.beginLine);
    const std::size_t gapLast = std::max(moving.endLine, neighbour.endLine);
    const std::size_t gapStart = moveUp ? neighbour.endLine + 1 : moving.endLine + 1;
    const std::size_t gapEnd = moveUp ? moving.beginLine : neighbour.beginLine;  // exclusive

    const std::vector<std::string> gap(
        lines.begin() + static_cast<std::ptrdiff_t>(gapStart),
        lines.begin() + static_cast<std::ptrdiff_t>(gapEnd));

    std::vector<std::string> replacement;

    const auto append = [&replacement](const std::vector<std::string>& block)
    {
        replacement.insert(replacement.end(), block.begin(), block.end());
    };

    append(moveUp ? movingBlock : neighbourBlock);
    append(gap);
    append(moveUp ? neighbourBlock : movingBlock);

    lines.erase(
        lines.begin() + static_cast<std::ptrdiff_t>(gapFirst),
        lines.begin() + static_cast<std::ptrdiff_t>(gapLast) + 1);
    lines.insert(
        lines.begin() + static_cast<std::ptrdiff_t>(gapFirst),
        replacement.begin(),
        replacement.end());

    updatedText = JoinLines(lines, DetectLineEnding(text));
    return true;
}

bool ReadModsIni(
    const std::filesystem::path& modsFolder,
    ModsIniDocument& document,
    bool& exists,
    std::string& error)
{
    document.entries.clear();
    exists = false;
    error.clear();

    std::string bom;
    std::string text;

    if (!ReadListText(modsFolder / kModsIniFileName, bom, text, exists, error))
    {
        return false;
    }

    return ParseModsIni(text, document, error);
}

bool ReadModTitle(
    const std::filesystem::path& modDirectory,
    std::string& title,
    std::string& error)
{
    title.clear();
    error.clear();

    std::string bom;
    std::string text;
    bool exists = false;

    if (!ReadListText(modDirectory / kModMetadataFileName, bom, text, exists, error))
    {
        return false;
    }

    if (!exists)
    {
        // A mod without a manifest keeps whatever name the caller has for it.
        return true;
    }

    // The manifest is the game's own "struct" markup as well, and "title" sits in
    // its top level. Nested blocks (a change note, for instance) come after it and
    // use other keys, so the first match is the name.
    for (const std::string& line : SplitLines(text))
    {
        const Token token = Tokenize(line);

        if (token.kind == TokenKind::Value && EqualsIgnoreCase(token.key, "title"))
        {
            title = token.value;
            break;
        }
    }

    return true;
}

bool EnsureModsIniStates(
    const std::filesystem::path& modsFolder,
    const std::vector<ModsIniRecordState>& states,
    bool& changed,
    std::string& error)
{
    changed = false;

    const fs::path filePath = modsFolder / kModsIniFileName;

    std::string bom;
    std::string bytes;
    bool exists = false;

    if (!ReadListText(filePath, bom, bytes, exists, error))
    {
        return false;
    }

    std::string updated;

    if (!ApplyModsIniStates(bytes, states, updated, error))
    {
        return false;
    }

    if (updated.empty())
    {
        // Everything is already as requested.
        return true;
    }

    if (!WriteListText(filePath, bom, updated, error))
    {
        return false;
    }

    changed = true;
    return true;
}

bool MoveModsIniRecord(
    const std::filesystem::path& modsFolder,
    const std::string& dir,
    bool moveUp,
    bool& changed,
    std::string& error)
{
    changed = false;

    const std::string target = Trim(dir);

    if (NormalizeModsIniDir(target).empty())
    {
        error = "the mod directory is empty";
        return false;
    }

    const fs::path filePath = modsFolder / kModsIniFileName;

    std::string bom;
    std::string bytes;
    bool exists = false;

    if (!ReadListText(filePath, bom, bytes, exists, error))
    {
        return false;
    }

    // A record the file does not have yet has no place to move to, so it is listed
    // the way switching the mod on lists it - switched on, at the end - and the move
    // then carries it to where the user asked for.
    ModsIniDocument document;

    if (!ParseModsIni(bytes, document, error))
    {
        return false;
    }

    bool listed = false;

    for (const ModsIniEntry& entry : document.entries)
    {
        if (SameModsIniDir(entry.dir, target))
        {
            listed = true;
            break;
        }
    }

    std::string current = bytes;

    // Whether the text the editor produced differs from the file, i.e. whether
    // something has to be written at all.
    bool updated = false;

    if (!listed)
    {
        std::string added;

        if (!ApplyModsIniStates(
                current,
                { ModsIniRecordState{ target, ModsIniState::Enabled } },
                added,
                error))
        {
            return false;
        }

        if (!added.empty())
        {
            current = std::move(added);
            updated = true;
        }
    }

    std::string moved;

    if (!SwapModsIniRecords(current, target, moveUp, moved, error))
    {
        return false;
    }

    if (!moved.empty())
    {
        current = std::move(moved);
        updated = true;
    }

    if (!updated)
    {
        // The record is already where it should be.
        return true;
    }

    if (!WriteListText(filePath, bom, current, error))
    {
        // Nothing was written, so the file is still the one that was read.
        changed = false;
        return false;
    }

    changed = true;
    return true;
}

} // namespace core

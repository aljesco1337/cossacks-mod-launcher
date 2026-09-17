#include "GameIni.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <vector>

namespace core {

namespace {

constexpr const char* kUtf8Bom = "\xEF\xBB\xBF";
constexpr const char* kSectionBegin = "section.begin";
constexpr const char* kSectionEnd = "section.end";

// Keys of the section the engine's own settings live in. When neither logging
// switch exists yet, the new lines go next to these instead of into whatever
// section happens to come first.
constexpr const char* kSectionHints[] = {
    "GameSaveDirectoryPath",
    "LogFileName",
    "HideHelloScreen",
};

// One physical line, split into its text and its terminator, so the original
// style survives the round trip.
struct Line
{
    std::string text;
    std::string ending;
};

// A "Key = value" line and where its parts are in Line::text.
struct KeyLine
{
    std::size_t index = 0;
    std::string key;
    std::string value;
    std::string text;        // the line as parsed; the spans below refer to it
    std::size_t keyBegin = 0;
    std::size_t keyEnd = 0;
    std::size_t valueBegin = 0;
    std::size_t valueEnd = 0;
};

struct Section
{
    std::size_t begin = 0;  // "section.begin" line
    std::size_t end = 0;    // "section.end" line
};

bool IsHorizontalSpace(char value)
{
    return value == ' ' || value == '\t';
}

bool IsSpace(char value)
{
    return IsHorizontalSpace(value) || value == '\r' || value == '\n';
}

std::string Trim(const std::string& value)
{
    std::size_t begin = 0;
    std::size_t end = value.size();

    while (begin < end && IsSpace(value[begin]))
    {
        begin++;
    }

    while (end > begin && IsSpace(value[end - 1]))
    {
        end--;
    }

    return value.substr(begin, end - begin);
}

char ToLower(char value)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

bool EqualsIgnoreCase(const std::string& left, const std::string& right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); index++)
    {
        if (ToLower(left[index]) != ToLower(right[index]))
        {
            return false;
        }
    }

    return true;
}

// The only encoding the engine writes; anything else is refused so a binary
// file is never "edited" into garbage.
bool IsUtf16(const std::string& text)
{
    if (text.size() < 2)
    {
        return false;
    }

    const auto first = static_cast<unsigned char>(text[0]);
    const auto second = static_cast<unsigned char>(text[1]);

    return (first == 0xFF && second == 0xFE) || (first == 0xFE && second == 0xFF);
}

std::vector<Line> SplitLines(const std::string& text)
{
    std::vector<Line> lines;

    std::size_t position = 0;

    while (position < text.size())
    {
        const std::size_t lineEnd = text.find('\n', position);
        std::size_t textEnd = lineEnd == std::string::npos ? text.size() : lineEnd;

        Line line;

        if (textEnd > position && text[textEnd - 1] == '\r')
        {
            line.ending = "\r\n";
            textEnd--;
        }
        else if (lineEnd != std::string::npos)
        {
            line.ending = "\n";
        }

        line.text = text.substr(position, textEnd - position);
        lines.push_back(std::move(line));

        position = lineEnd == std::string::npos ? text.size() : lineEnd + 1;
    }

    return lines;
}

std::string JoinLines(const std::vector<Line>& lines)
{
    std::string text;

    for (const Line& line : lines)
    {
        text += line.text;
        text += line.ending;
    }

    return text;
}

// Splits "   LogFileEnabled = true" into its key and the span of its value, so
// a rewritten line keeps every character around them.
std::optional<KeyLine> ParseKeyLine(std::size_t index, const Line& line)
{
    const std::string trimmed = Trim(line.text);

    if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#' ||
        trimmed.starts_with("//"))
    {
        return std::nullopt;
    }

    const std::size_t equals = line.text.find('=');

    if (equals == std::string::npos)
    {
        return std::nullopt;
    }

    std::size_t keyBegin = 0;
    while (keyBegin < equals && IsSpace(line.text[keyBegin]))
    {
        keyBegin++;
    }

    std::size_t keyEnd = equals;
    while (keyEnd > keyBegin && IsSpace(line.text[keyEnd - 1]))
    {
        keyEnd--;
    }

    if (keyBegin == keyEnd)
    {
        return std::nullopt;
    }

    const char first = line.text[keyBegin];
    if (!std::isalpha(static_cast<unsigned char>(first)) && first != '_')
    {
        return std::nullopt;
    }

    std::size_t valueBegin = equals + 1;
    while (valueBegin < line.text.size() && IsHorizontalSpace(line.text[valueBegin]))
    {
        valueBegin++;
    }

    std::size_t valueEnd = valueBegin;
    while (valueEnd < line.text.size() && !IsHorizontalSpace(line.text[valueEnd]))
    {
        valueEnd++;
    }

    KeyLine result;
    result.index = index;
    result.text = line.text;
    result.keyBegin = keyBegin;
    result.keyEnd = keyEnd;
    result.valueBegin = valueBegin;
    result.valueEnd = valueEnd;
    result.key = line.text.substr(keyBegin, keyEnd - keyBegin);
    result.value = line.text.substr(valueBegin, valueEnd - valueBegin);

    return result;
}

bool ParseBool(const std::string& value, bool fallback)
{
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), ToLower);

    if (lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "on")
    {
        return true;
    }

    if (lowered == "false" || lowered == "0" || lowered == "no" || lowered == "off")
    {
        return false;
    }

    return fallback;
}

// Writes the value in the spelling the file already uses, so "True" stays
// capitalised and "1" stays a number.
std::string FormatBool(bool value, const std::string& reference)
{
    if (reference == "1" || reference == "0")
    {
        return value ? "1" : "0";
    }

    if (reference == "TRUE" || reference == "FALSE")
    {
        return value ? "TRUE" : "FALSE";
    }

    const bool capitalised =
        !reference.empty() &&
        std::isupper(static_cast<unsigned char>(reference[0])) != 0;

    if (!capitalised)
    {
        return value ? "true" : "false";
    }

    return value ? "True" : "False";
}

// Reuses an existing line as the template for the sibling key that the file
// does not have yet, which keeps its indentation and spacing. The spans come
// from the parsed source line, so it does not matter whether that line has
// already been rewritten in the meantime.
std::string BuildKeyLine(const KeyLine& source, const std::string& key, bool value)
{
    std::string result = source.text;

    result.replace(
        source.valueBegin,
        source.valueEnd - source.valueBegin,
        FormatBool(value, source.value));

    result.replace(source.keyBegin, source.keyEnd - source.keyBegin, key);

    return result;
}

bool IsSectionMarker(const std::string& trimmed, const char* marker)
{
    const std::size_t length = std::char_traits<char>::length(marker);

    return trimmed.size() >= length &&
        EqualsIgnoreCase(trimmed.substr(0, length), marker);
}

std::vector<Section> CollectSections(const std::vector<Line>& lines)
{
    std::vector<Section> sections;
    std::optional<std::size_t> open;

    for (std::size_t index = 0; index < lines.size(); index++)
    {
        const std::string trimmed = Trim(lines[index].text);

        if (IsSectionMarker(trimmed, kSectionBegin))
        {
            if (!open)
            {
                open = index;
            }
        }
        else if (IsSectionMarker(trimmed, kSectionEnd) && open)
        {
            sections.push_back(Section{ *open, index });
            open.reset();
        }
    }

    return sections;
}

bool IsHintKey(const std::string& key)
{
    for (const char* hint : kSectionHints)
    {
        if (EqualsIgnoreCase(key, hint))
        {
            return true;
        }
    }

    return false;
}

std::string DominantEnding(const std::vector<Line>& lines)
{
    std::size_t carriageReturns = 0;
    std::size_t lineFeeds = 0;

    for (const Line& line : lines)
    {
        if (line.ending == "\r\n")
        {
            carriageReturns++;
        }
        else if (line.ending == "\n")
        {
            lineFeeds++;
        }
    }

    return carriageReturns > lineFeeds ? "\r\n" : "\n";
}

} // namespace

LogIniSettings ReadLogSettings(const std::string& text)
{
    LogIniSettings settings;

    std::string body = text;
    if (body.starts_with(kUtf8Bom))
    {
        body.erase(0, 3);
    }

    bool foundEnabled = false;
    bool foundRoot = false;

    for (const Line& line : SplitLines(body))
    {
        const auto parsed = ParseKeyLine(0, line);
        if (!parsed)
        {
            continue;
        }

        if (EqualsIgnoreCase(parsed->key, kLogEnabledKey))
        {
            settings.enabled = ParseBool(parsed->value, false);
            foundEnabled = true;
        }
        else if (EqualsIgnoreCase(parsed->key, kLogRootKey))
        {
            settings.root = ParseBool(parsed->value, false);
            foundRoot = true;
        }
    }

    settings.present = foundEnabled && foundRoot;

    return settings;
}

bool SetLogSettings(
    const std::string& text,
    bool enabled,
    std::string& updated,
    std::string& error)
{
    if (IsUtf16(text))
    {
        error = "The file is not UTF-8 encoded.";
        return false;
    }

    std::string bom;
    std::string body = text;

    if (body.starts_with(kUtf8Bom))
    {
        bom = kUtf8Bom;
        body.erase(0, 3);
    }

    std::vector<Line> lines = SplitLines(body);

    std::vector<KeyLine> keyLines;
    std::optional<KeyLine> enabledLine;
    std::optional<KeyLine> rootLine;

    for (std::size_t index = 0; index < lines.size(); index++)
    {
        const auto parsed = ParseKeyLine(index, lines[index]);
        if (!parsed)
        {
            continue;
        }

        keyLines.push_back(*parsed);

        if (EqualsIgnoreCase(parsed->key, kLogEnabledKey))
        {
            enabledLine = parsed;
        }
        else if (EqualsIgnoreCase(parsed->key, kLogRootKey))
        {
            rootLine = parsed;
        }
    }

    const auto rewriteValue = [&lines](const KeyLine& key, bool value)
    {
        Line& line = lines[key.index];
        line.text.replace(
            key.valueBegin,
            key.valueEnd - key.valueBegin,
            FormatBool(value, key.value));
    };

    // At least one switch is there: update it and, when the other one is
    // missing, add it as its direct sibling so the two stay together.
    if (enabledLine || rootLine)
    {
        if (enabledLine)
        {
            rewriteValue(*enabledLine, enabled);
        }

        if (rootLine)
        {
            rewriteValue(*rootLine, enabled);
        }

        if (!enabledLine)
        {
            const std::string added = BuildKeyLine(*rootLine, kLogEnabledKey, enabled);
            lines.insert(
                lines.begin() + static_cast<std::ptrdiff_t>(rootLine->index + 1),
                Line{ added, lines[rootLine->index].ending });
        }
        else if (!rootLine)
        {
            const std::string added = BuildKeyLine(*enabledLine, kLogRootKey, enabled);
            lines.insert(
                lines.begin() + static_cast<std::ptrdiff_t>(enabledLine->index + 1),
                Line{ added, lines[enabledLine->index].ending });
        }

        updated = bom + JoinLines(lines);
        return true;
    }

    // Neither switch exists: put both into the section holding the engine's own
    // settings, which is the one they belong to.
    const std::vector<Section> sections = CollectSections(lines);

    const Section* target = nullptr;

    for (const Section& section : sections)
    {
        const bool hasHint = std::any_of(
            keyLines.begin(),
            keyLines.end(),
            [&section](const KeyLine& key)
            {
                return key.index > section.begin &&
                    key.index < section.end &&
                    IsHintKey(key.key);
            });

        if (hasHint)
        {
            target = &section;
            break;
        }
    }

    if (!target && !sections.empty())
    {
        target = &sections.front();
    }

    if (!target)
    {
        // No section block at all: append a fresh one, because keys outside a
        // section are not read by the engine.
        const std::string ending = DominantEnding(lines);

        if (!lines.empty() && lines.back().ending.empty())
        {
            lines.back().ending = ending;
        }

        lines.push_back(Line{ std::string(kSectionBegin), ending });
        lines.push_back(Line{ std::string("   ") + kLogEnabledKey + " = " + FormatBool(enabled, "true"), ending });
        lines.push_back(Line{ std::string("   ") + kLogRootKey + " = " + FormatBool(enabled, "true"), ending });
        lines.push_back(Line{ std::string(kSectionEnd), ending });

        updated = bom + JoinLines(lines);
        return true;
    }

    // Copy the indentation and the line ending style of the section's keys.
    std::string indent = "   ";
    std::string ending = lines[target->end].ending.empty() ? "\n" : lines[target->end].ending;

    for (const KeyLine& key : keyLines)
    {
        if (key.index > target->begin && key.index < target->end)
        {
            indent = lines[key.index].text.substr(0, key.keyBegin);

            if (!lines[key.index].ending.empty())
            {
                ending = lines[key.index].ending;
            }

            break;
        }
    }

    std::vector<Line> inserted;
    inserted.push_back(Line{ indent + kLogEnabledKey + " = " + FormatBool(enabled, "true"), ending });
    inserted.push_back(Line{ indent + kLogRootKey + " = " + FormatBool(enabled, "true"), ending });

    // Directly before the "section.end" that closes the section.
    lines.insert(
        lines.begin() + static_cast<std::ptrdiff_t>(target->end),
        inserted.begin(),
        inserted.end());

    updated = bom + JoinLines(lines);
    return true;
}

} // namespace core

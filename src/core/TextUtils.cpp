#include "TextUtils.h"

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <sstream>
#include <string_view>

namespace core {

namespace {

void AppendCodePoint(std::wstring& out, uint32_t codePoint)
{
    if constexpr (sizeof(wchar_t) >= 4)
    {
        out.push_back(static_cast<wchar_t>(codePoint));
    }
    else
    {
        // wchar_t is 16-bit (UTF-16): encode surrogate pairs for astral chars.
        if (codePoint < 0x10000)
        {
            out.push_back(static_cast<wchar_t>(codePoint));
        }
        else
        {
            codePoint -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (codePoint >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (codePoint & 0x3FF)));
        }
    }
}

bool StartsWith(const std::string& bytes, std::initializer_list<unsigned char> prefix)
{
    if (bytes.size() < prefix.size())
    {
        return false;
    }

    size_t index = 0;
    for (unsigned char value : prefix)
    {
        if (static_cast<unsigned char>(bytes[index]) != value)
        {
            return false;
        }
        index++;
    }

    return true;
}

// Decodes a strict UTF-8 byte range into a wide string. Returns false on
// malformed input (this mirrors the behaviour of MultiByteToWideChar with
// MB_ERR_INVALID_CHARS).
bool DecodeUtf8(const char* data, size_t length, std::wstring& out)
{
    out.clear();
    out.reserve(length);

    size_t index = 0;
    while (index < length)
    {
        const unsigned char lead = static_cast<unsigned char>(data[index]);

        uint32_t codePoint = 0;
        size_t extra = 0;

        if (lead < 0x80)
        {
            codePoint = lead;
        }
        else if ((lead >> 5) == 0x6) // 110xxxxx
        {
            codePoint = lead & 0x1F;
            extra = 1;
        }
        else if ((lead >> 4) == 0xE) // 1110xxxx
        {
            codePoint = lead & 0x0F;
            extra = 2;
        }
        else if ((lead >> 3) == 0x1E) // 11110xxx
        {
            codePoint = lead & 0x07;
            extra = 3;
        }
        else
        {
            return false;
        }

        if (index + extra >= length)
        {
            return false;
        }

        for (size_t offset = 0; offset < extra; offset++)
        {
            const unsigned char continuation =
                static_cast<unsigned char>(data[index + 1 + offset]);

            if ((continuation >> 6) != 0x2)
            {
                return false;
            }

            codePoint = (codePoint << 6) | (continuation & 0x3F);
        }

        AppendCodePoint(out, codePoint);
        index += 1 + extra;
    }

    return true;
}

std::wstring DecodeUtf16(const std::string& data, size_t offset, bool bigEndian)
{
    std::wstring out;
    out.reserve((data.size() - offset) / 2);

    for (size_t index = offset; index + 1 < data.size(); index += 2)
    {
        const unsigned char first = static_cast<unsigned char>(data[index]);
        const unsigned char second = static_cast<unsigned char>(data[index + 1]);

        const uint16_t unit = bigEndian
            ? static_cast<uint16_t>((first << 8) | second)
            : static_cast<uint16_t>((second << 8) | first);

        if constexpr (sizeof(wchar_t) == 2)
        {
            out.push_back(static_cast<wchar_t>(unit));
        }
        else
        {
            // 32-bit wchar_t: recombine UTF-16 surrogate pairs into code points.
            if (unit >= 0xD800 && unit <= 0xDBFF && index + 3 < data.size())
            {
                const unsigned char lowFirst =
                    static_cast<unsigned char>(data[index + 2]);
                const unsigned char lowSecond =
                    static_cast<unsigned char>(data[index + 3]);

                const uint16_t low = bigEndian
                    ? static_cast<uint16_t>((lowFirst << 8) | lowSecond)
                    : static_cast<uint16_t>((lowSecond << 8) | lowFirst);

                if (low >= 0xDC00 && low <= 0xDFFF)
                {
                    const uint32_t codePoint = 0x10000 +
                        ((static_cast<uint32_t>(unit) - 0xD800) << 10) +
                        (static_cast<uint32_t>(low) - 0xDC00);

                    AppendCodePoint(out, codePoint);
                    index += 2;
                    continue;
                }
            }

            out.push_back(static_cast<wchar_t>(unit));
        }
    }

    return out;
}

// Windows-1251 code points for bytes 0x80..0xFF.
constexpr uint32_t kCp1251High[128] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
    0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0xFFFD, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
    0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
    0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
    0x0410, 0x0411, 0x0412, 0x0413, 0x0414, 0x0415, 0x0416, 0x0417,
    0x0418, 0x0419, 0x041A, 0x041B, 0x041C, 0x041D, 0x041E, 0x041F,
    0x0420, 0x0421, 0x0422, 0x0423, 0x0424, 0x0425, 0x0426, 0x0427,
    0x0428, 0x0429, 0x042A, 0x042B, 0x042C, 0x042D, 0x042E, 0x042F,
    0x0430, 0x0431, 0x0432, 0x0433, 0x0434, 0x0435, 0x0436, 0x0437,
    0x0438, 0x0439, 0x043A, 0x043B, 0x043C, 0x043D, 0x043E, 0x043F,
    0x0440, 0x0441, 0x0442, 0x0443, 0x0444, 0x0445, 0x0446, 0x0447,
    0x0448, 0x0449, 0x044A, 0x044B, 0x044C, 0x044D, 0x044E, 0x044F,
};

std::wstring DecodeCp1251(const std::string& data, size_t offset)
{
    std::wstring out;
    out.reserve(data.size() - offset);

    for (size_t index = offset; index < data.size(); index++)
    {
        const unsigned char value = static_cast<unsigned char>(data[index]);

        if (value < 0x80)
        {
            out.push_back(static_cast<wchar_t>(value));
        }
        else
        {
            AppendCodePoint(out, kCp1251High[value - 0x80]);
        }
    }

    return out;
}

} // namespace

std::wstring Trim(std::wstring value)
{
    auto isSpace = [](wchar_t character)
    {
        return std::iswspace(character) != 0;
    };

    value.erase(
        value.begin(),
        std::find_if(value.begin(), value.end(), [isSpace](wchar_t character) { return !isSpace(character); })
    );

    value.erase(
        std::find_if(value.rbegin(), value.rend(), [isSpace](wchar_t character) { return !isSpace(character); }).base(),
        value.end()
    );

    return value;
}

std::wstring CleanConfigValue(const std::wstring& value)
{
    std::wstring result = Trim(value);

    if (!result.empty() && result.back() == L';')
    {
        result.pop_back();
        result = Trim(result);
    }

    if (result.size() >= 2)
    {
        wchar_t first = result.front();
        wchar_t last = result.back();
        if ((first == L'\'' && last == L'\'') || (first == L'"' && last == L'"'))
        {
            result = Trim(result.substr(1, result.size() - 2));
        }
    }

    return result;
}

std::wstring ExpandTabs(const std::wstring& text, int tabSize)
{
    std::wstring result;
    int column = 0;

    for (wchar_t character : text)
    {
        if (character == L'\t')
        {
            int spaces = tabSize - (column % tabSize);
            result.append(static_cast<size_t>(spaces), L' ');
            column += spaces;
        }
        else
        {
            result.push_back(character);
            column++;
        }
    }

    return result;
}

std::vector<std::wstring> SplitLinesPreserveTrailing(const std::wstring& text)
{
    std::vector<std::wstring> lines;
    if (text.empty()) return lines;

    std::wstring current;

    for (size_t index = 0; index < text.size(); index++)
    {
        wchar_t character = text[index];

        if (character == L'\r')
        {
            lines.push_back(current);
            current.clear();

            if (index + 1 < text.size() && text[index + 1] == L'\n')
            {
                index++;
            }
        }
        else if (character == L'\n')
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

std::wstring NormalizeLineEndings(const std::wstring& text)
{
    std::wstring result;
    result.reserve(text.size() + 128);

    for (size_t i = 0; i < text.size(); i++)
    {
        wchar_t ch = text[i];

        if (ch == L'\r')
        {
            result += L'\r';

            if (i + 1 < text.size() && text[i + 1] == L'\n')
            {
                result += L'\n';
                i++;
            }
            else
            {
                result += L'\n';
            }
        }
        else if (ch == L'\n')
        {
            result += L"\r\n";
        }
        else
        {
            result += ch;
        }
    }

    return result;
}

std::wstring FormatPreviewText(const std::wstring& text)
{
    std::wstring normalized = NormalizeLineEndings(text);
    std::wstringstream input(normalized);

    std::wstring line;
    std::wstring result;
    bool first = true;

    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == L'\r') line.pop_back();

        if (line.starts_with(L"ERR|") || line.starts_with(L"LOG|") || line.starts_with(L"TIM|"))
            line = L"  " + line;

        if (!first) result += L"\r\n";
        result += line;
        first = false;
    }

    return result;
}

LogSeverityCounts CountLogSeverity(const std::wstring& text)
{
    LogSeverityCounts counts;
    bool hasCompileFrameworkCritical = false;
    std::wstring normalized = NormalizeLineEndings(text);
    std::wstringstream input(normalized);

    std::wstring line;
    while (std::getline(input, line))
    {
        if (!line.empty() && line.back() == L'\r') line.pop_back();

        size_t firstText = line.find_first_not_of(L" \t");
        std::wstring_view trimmed = firstText == std::wstring::npos
            ? std::wstring_view()
            : std::wstring_view(line).substr(firstText);

        if (trimmed.starts_with(L"ERR") || trimmed.starts_with(L"ERROR"))
        {
            counts.errors++;
        }

        const bool hasSyntaxCritical =
            trimmed.find(L"CompileFramework()") != std::wstring_view::npos &&
            trimmed.find(L"compile global script error: Syntax error: Line:") != std::wstring_view::npos &&
            trimmed.find(L"Column") != std::wstring_view::npos;

        const bool hasCompileScriptError =
            trimmed.find(L"Compile script error") != std::wstring_view::npos;

        if (hasSyntaxCritical)
        {
            hasCompileFrameworkCritical = true;
        }
        else if (hasCompileScriptError)
        {
            counts.critical++;
        }
    }

    if (hasCompileFrameworkCritical)
    {
        counts.critical = 1;
    }

    return counts;
}

std::wstring ReadTextFile(const std::wstring& path)
{
    std::ifstream file(fs::path(path), std::ios::binary);

    if (!file.is_open())
    {
        return L"Cannot open file:\r\n" + path;
    }

    std::string bytes{
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()
    };

    if (bytes.empty())
    {
        return {};
    }

    if (StartsWith(bytes, { 0xEF, 0xBB, 0xBF }))
    {
        std::wstring decoded;
        if (DecodeUtf8(bytes.data() + 3, bytes.size() - 3, decoded))
        {
            return decoded;
        }
        return {};
    }

    if (StartsWith(bytes, { 0xFF, 0xFE }))
    {
        return DecodeUtf16(bytes, 2, false);
    }

    if (StartsWith(bytes, { 0xFE, 0xFF }))
    {
        return DecodeUtf16(bytes, 2, true);
    }

    std::wstring utf8;
    if (DecodeUtf8(bytes.data(), bytes.size(), utf8))
    {
        return utf8;
    }

    return DecodeCp1251(bytes, 0);
}

} // namespace core

#include "ZipArchive.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "PathUtils.h"
#include "miniz.h"

namespace core {

namespace {

using Path = std::filesystem::path;

// Starting size of the heap buffer a new archive is built in; miniz grows it
// as needed.
constexpr std::size_t kZipInitialSize = 1u << 20;

// Rejects anything that could escape the extraction directory once combined
// with it. Callers pass paths that already went through lexically_normal(),
// so "a/../../b" shows up here as "../b".
bool IsSafeRelativePath(const Path& relative)
{
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() ||
        relative.has_root_directory())
    {
        return false;
    }

    for (const Path& part : relative)
    {
        if (part == Path("..") || part == Path("."))
        {
            return false;
        }
    }

    return true;
}

bool IsDotComponent(const Path& relative)
{
    return relative.empty() || relative == Path(".");
}

bool ReadWholeFile(const Path& filePath, std::vector<std::uint8_t>& data, std::wstring& error)
{
    std::ifstream input(filePath, std::ios::binary);
    if (!input)
    {
        error = L"Cannot open the downloaded archive.";
        return false;
    }

    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();

    if (length < 0)
    {
        error = L"Cannot determine the size of the downloaded archive.";
        return false;
    }

    input.seekg(0, std::ios::beg);

    data.resize(static_cast<std::size_t>(length));

    if (length > 0)
    {
        input.read(reinterpret_cast<char*>(data.data()), length);

        if (!input)
        {
            error = L"Cannot read the downloaded archive.";
            return false;
        }
    }

    return true;
}

// Closes the miniz reader on every exit path.
class ZipReader
{
public:
    ZipReader() = default;
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;

    ~ZipReader()
    {
        if (open_)
        {
            mz_zip_reader_end(&archive_);
        }
    }

    bool Open(const std::vector<std::uint8_t>& data)
    {
        open_ = mz_zip_reader_init_mem(&archive_, data.data(), data.size(), 0) != MZ_FALSE;
        return open_;
    }

    mz_zip_archive* get() { return &archive_; }

private:
    mz_zip_archive archive_{};
    bool open_ = false;
};

struct ZipEntry
{
    mz_uint index = 0;
    Path relativePath;
    bool isDirectory = false;
    std::uint64_t uncompressedSize = 0;
};

// Reads the table of contents. An unsafe or unreadable entry aborts the scan
// so the caller refuses the archive instead of extracting it partially.
bool CollectEntries(ZipReader& reader, std::vector<ZipEntry>& entries, std::wstring& error)
{
    mz_zip_archive* archive = reader.get();
    const mz_uint count = mz_zip_reader_get_num_files(archive);

    entries.clear();
    entries.reserve(count);

    for (mz_uint index = 0; index < count; index++)
    {
        const mz_uint nameLength = mz_zip_reader_get_filename(archive, index, nullptr, 0);
        if (nameLength == 0)
        {
            error = L"The archive contains an entry without a name.";
            return false;
        }

        std::string name(nameLength, '\0');
        mz_zip_reader_get_filename(archive, index, name.data(), nameLength);

        while (!name.empty() && name.back() == '\0')
        {
            name.pop_back();
        }

        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(archive, index, &stat))
        {
            error = L"The archive is corrupted.";
            return false;
        }

        const Path relative = Utf8ToPath(name).lexically_normal();

        if (IsDotComponent(relative))
        {
            // A bare "./" entry carries nothing to extract.
            continue;
        }

        if (!IsSafeRelativePath(relative))
        {
            error = L"The archive contains an unsafe path and was rejected.";
            return false;
        }

        ZipEntry entry;
        entry.index = index;
        entry.relativePath = relative;
        entry.isDirectory = mz_zip_reader_is_file_a_directory(archive, index) != MZ_FALSE;
        entry.uncompressedSize = stat.m_uncomp_size;

        entries.push_back(std::move(entry));
    }

    return true;
}

// Removes "prefix" from the front of "relative". Entries that do not start
// with the prefix come back unchanged, so a wrong hint degrades into a plain
// extraction instead of losing files.
Path StripPrefix(const Path& relative, const Path& prefix)
{
    if (prefix.empty())
    {
        return relative;
    }

    const std::vector<Path> parts(relative.begin(), relative.end());
    const std::vector<Path> leading(prefix.begin(), prefix.end());

    if (parts.size() < leading.size())
    {
        return relative;
    }

    for (std::size_t index = 0; index < leading.size(); index++)
    {
        if (parts[index] != leading[index])
        {
            return relative;
        }
    }

    Path stripped;

    for (std::size_t index = leading.size(); index < parts.size(); index++)
    {
        stripped /= parts[index];
    }

    return stripped;
}

std::size_t WriteToStream(void* opaque, mz_uint64 /*fileOffset*/, const void* buffer, std::size_t count)
{
    auto* output = static_cast<std::ofstream*>(opaque);
    output->write(static_cast<const char*>(buffer), static_cast<std::streamsize>(count));

    return output->good() ? count : 0;
}

// Guards against archives that expand to an absurd size.
constexpr std::uint64_t kMaxTotalUncompressedBytes = 4ull * 1024ull * 1024ull * 1024ull;

} // namespace

bool FindZipRootFolder(const std::filesystem::path& archivePath, std::filesystem::path& rootFolder, std::wstring& error)
{
    rootFolder.clear();
    error.clear();

    std::vector<std::uint8_t> data;

    if (!ReadWholeFile(archivePath, data, error))
    {
        return false;
    }

    ZipReader reader;

    if (!reader.Open(data))
    {
        error = L"The downloaded file is not a valid ZIP archive.";
        return false;
    }

    std::vector<ZipEntry> entries;

    if (!CollectEntries(reader, entries, error))
    {
        return false;
    }

    Path root;
    bool hasRoot = false;
    bool hasNestedEntry = false;

    for (const ZipEntry& entry : entries)
    {
        auto part = entry.relativePath.begin();

        if (part == entry.relativePath.end())
        {
            continue;
        }

        const Path first = *part;

        if (!hasRoot)
        {
            root = first;
            hasRoot = true;
        }
        else if (first != root)
        {
            // Several top level entries: there is nothing to strip.
            return true;
        }

        if (++part != entry.relativePath.end())
        {
            hasNestedEntry = true;
        }
    }

    // A single file sitting at the archive root is a name, not a folder.
    if (hasRoot && hasNestedEntry)
    {
        rootFolder = root;
    }

    return true;
}

bool ExtractZip(
    const std::filesystem::path& archivePath,
    const std::filesystem::path& destination,
    const std::filesystem::path& stripPrefix,
    const std::function<bool()>& isCancelled,
    std::wstring& error)
{
    error.clear();

    std::vector<std::uint8_t> data;

    if (!ReadWholeFile(archivePath, data, error))
    {
        return false;
    }

    ZipReader reader;

    if (!reader.Open(data))
    {
        error = L"The downloaded file is not a valid ZIP archive.";
        return false;
    }

    std::vector<ZipEntry> entries;

    if (!CollectEntries(reader, entries, error))
    {
        return false;
    }

    std::uint64_t totalSize = 0;

    for (const ZipEntry& entry : entries)
    {
        totalSize += entry.uncompressedSize;
    }

    if (totalSize > kMaxTotalUncompressedBytes)
    {
        error = L"The archive expands to an unreasonable size and was rejected.";
        return false;
    }

    Path prefix;

    if (!stripPrefix.empty())
    {
        prefix = stripPrefix.lexically_normal();

        if (!IsSafeRelativePath(prefix))
        {
            error = L"The manifest describes an unsafe archive layout.";
            return false;
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(destination, ec);

    if (ec)
    {
        error = L"Cannot create the folder used to extract the mod.";
        return false;
    }

    for (const ZipEntry& entry : entries)
    {
        if (isCancelled && isCancelled())
        {
            error = L"Installation cancelled.";
            return false;
        }

        const Path relative = StripPrefix(entry.relativePath, prefix);

        if (IsDotComponent(relative))
        {
            // The stripped entry was the wrapper folder itself.
            continue;
        }

        const Path target = destination / relative;

        if (entry.isDirectory)
        {
            std::filesystem::create_directories(target, ec);

            if (ec)
            {
                error = L"Cannot create a folder while extracting the mod.";
                return false;
            }

            continue;
        }

        std::filesystem::create_directories(target.parent_path(), ec);

        if (ec)
        {
            error = L"Cannot create a folder while extracting the mod.";
            return false;
        }

        std::ofstream output(target, std::ios::binary | std::ios::trunc);

        if (!output)
        {
            error = L"Cannot write an extracted file.";
            return false;
        }

        if (!mz_zip_reader_extract_to_callback(reader.get(), entry.index, WriteToStream, &output, 0))
        {
            error = L"The archive is corrupted (an entry could not be extracted).";
            return false;
        }

        output.close();

        if (!output)
        {
            error = L"Cannot write an extracted file (the disk may be full).";
            return false;
        }
    }

    return true;
}

bool CreateZipFromFolder(
    const std::filesystem::path& sourceFolder,
    const std::string& entryPrefix,
    const std::filesystem::path& archivePath,
    std::size_t& fileCount,
    std::wstring& error)
{
    fileCount = 0;

    std::error_code ec;

    if (!std::filesystem::is_directory(sourceFolder, ec))
    {
        error = L"The folder that should be archived does not exist.";
        return false;
    }

    // Resolved once so the archive is never packed into itself when the user
    // saves it next to the files it contains.
    std::error_code absoluteError;
    const Path absoluteArchive = std::filesystem::absolute(archivePath, absoluteError);

    std::vector<Path> files;

    for (std::filesystem::recursive_directory_iterator it(sourceFolder, ec), end;
         !ec && it != end;
         it.increment(ec))
    {
        if (!it->is_regular_file(ec))
        {
            continue;
        }

        const Path file = it->path();

        if (!absoluteError && std::filesystem::absolute(file, absoluteError) == absoluteArchive)
        {
            continue;
        }

        files.push_back(file);
    }

    if (files.empty())
    {
        error = L"There is nothing to archive.";
        return false;
    }

    // Stable order, so two exports of the same folder look the same.
    std::sort(files.begin(), files.end());

    mz_zip_archive archive{};

    if (mz_zip_writer_init_heap(&archive, 0, kZipInitialSize) == MZ_FALSE)
    {
        error = L"Cannot start the archive.";
        return false;
    }

    std::size_t added = 0;
    std::vector<std::uint8_t> data;

    for (const Path& file : files)
    {
        data.clear();

        // The message is discarded: an unreadable file is skipped rather than
        // failing the whole export.
        std::wstring readError;

        if (!ReadWholeFile(file, data, readError))
        {
            continue;
        }

        std::string entryName = PathToUtf8(file.lexically_relative(sourceFolder));

        if (!entryPrefix.empty())
        {
            entryName = entryPrefix + "/" + entryName;
        }

        // A null buffer would trip miniz's assertions for empty files.
        const char* contents = data.empty()
            ? ""
            : reinterpret_cast<const char*>(data.data());

        if (mz_zip_writer_add_mem(&archive, entryName.c_str(), contents, data.size(), MZ_DEFAULT_COMPRESSION) == MZ_FALSE)
        {
            continue;
        }

        added++;
    }

    if (added == 0)
    {
        mz_zip_writer_end(&archive);
        error = L"None of the files could be added to the archive.";
        return false;
    }

    void* buffer = nullptr;
    std::size_t bufferSize = 0;

    if (mz_zip_writer_finalize_heap_archive(&archive, &buffer, &bufferSize) == MZ_FALSE)
    {
        mz_zip_writer_end(&archive);
        error = L"The archive could not be finalized.";
        return false;
    }

    // The buffer is handed over by finalize_heap_archive(), so ending the
    // archive no longer owns it.
    mz_zip_writer_end(&archive);

    bool written = false;

    {
        std::ofstream output(archivePath, std::ios::binary | std::ios::trunc);

        if (output && bufferSize > 0)
        {
            output.write(static_cast<const char*>(buffer), static_cast<std::streamsize>(bufferSize));
            written = output.good();
        }
    }

    mz_free(buffer);

    if (!written)
    {
        std::error_code removeError;
        std::filesystem::remove(archivePath, removeError);

        error = L"Cannot write the archive (the disk may be full).";
        return false;
    }

    fileCount = added;

    return true;
}

} // namespace core

#include "ModInstaller.h"

#include <QDir>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "PathBridge.h"
#include "ModStateStore.h"
#include "core/ModManifest.h"
#include "core/ModsIni.h"
#include "core/PathUtils.h"
#include "core/Workshop.h"
#include "core/ZipArchive.h"

namespace mods {

namespace {

namespace fs = std::filesystem;

// Scratch folders live next to the mod folder, which keeps them on the same
// volume as the final destination - required for rename() to be atomic.
fs::path WorkPath(const fs::path& modDirectory, const std::string& id, const char* prefix)
{
    return modDirectory.parent_path() / core::Utf8ToPath(prefix + core::SanitizeFolderName(id, "mod"));
}

fs::path StagingPath(const fs::path& modDirectory, const std::string& id)
{
    return WorkPath(modDirectory, id, ".clv-staging-");
}

fs::path BackupPath(const fs::path& modDirectory, const std::string& id)
{
    return WorkPath(modDirectory, id, ".clv-backup-");
}

std::string Utf8Of(const fs::path& value)
{
    return core::PathToUtf8(value);
}

// The game only loads folders that its mod list mentions, so a freshly
// installed mod has to be registered in "<game folder>/mods/mods.ini". Existing
// records are never touched, which keeps the state of the other mods intact.
//
// Steam workshop items found next to the game installation are listed as well,
// but switched off: filling in the list must not change which mods the game
// loads.
bool RegisterInGameMods(
    const fs::path& gameDirectory,
    const fs::path& modDirectory,
    std::string& error)
{
    const fs::path modsFolder = gameDirectory / L"mods";

    // "dir" is relative to the GAME folder, not to the "mods" folder that holds
    // mods.ini: the game's own workshop entries read
    // "..\..\workshop\content\333420\<id>", which only resolves from
    // "<library>/steamapps/common/Cossacks 3". Our own record therefore has to
    // be "mods\Renaissance" and not "Renaissance".
    //
    // Textual on purpose: the folder was just created and may live outside the
    // game folder (a manifest hint can point anywhere), in which case the record
    // escapes it with ".." just like those workshop entries do.
    fs::path relative = modDirectory.lexically_relative(gameDirectory);

    if (relative.empty() || relative == fs::path(L"."))
    {
        relative = modDirectory.filename();
    }

    std::string ourDir = Utf8Of(relative);

    // The format uses Windows separators.
    std::replace(ourDir.begin(), ourDir.end(), '/', '\\');

    std::vector<core::ModsIniRecord> records;

    // Workshop items first, so our own record stays the last one appended - the
    // same place it ends up in a list that already exists.
    for (const std::string& workshopDir : core::EnumerateWorkshopModDirs(gameDirectory))
    {
        records.push_back(core::ModsIniRecord{ workshopDir, false });
    }

    records.push_back(core::ModsIniRecord{ ourDir, true });

    bool added = false;

    return core::EnsureModsIniRecords(modsFolder, records, added, error);
}

bool ComputeSha256(const QString& filePath, QString& digest, QString& error)
{
    QFile file(filePath);

    if (!file.open(QIODevice::ReadOnly))
    {
        error = file.errorString();
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);

    while (!file.atEnd())
    {
        const QByteArray chunk = file.read(1024 * 1024);

        if (chunk.isEmpty() && file.error() != QFile::NoError)
        {
            error = file.errorString();
            return false;
        }

        hash.addData(chunk);
    }

    digest = QString::fromLatin1(hash.result().toHex());
    return true;
}

} // namespace

QString ModInstaller::ModDirectory(const QString& gameDirectory, const core::ModRelease& release)
{
    return FromPath(core::ResolveInstallDir(ToPath(gameDirectory), release));
}

void ModInstaller::Recover(const QString& gameDirectory, const core::ModRelease& release)
{
    std::error_code ec;

    const fs::path modDirectory = core::ResolveInstallDir(ToPath(gameDirectory), release);
    const fs::path staging = StagingPath(modDirectory, release.id);
    const fs::path backup = BackupPath(modDirectory, release.id);

    // Staging content is scratch data by definition.
    std::filesystem::remove_all(staging, ec);

    if (!std::filesystem::exists(backup, ec))
    {
        return;
    }

    if (!std::filesystem::exists(modDirectory, ec))
    {
        // The swap was interrupted after the old folder was moved aside.
        std::filesystem::rename(backup, modDirectory, ec);

        if (!ec)
        {
            return;
        }
    }

    std::filesystem::remove_all(backup, ec);
}

ModInstaller::Result ModInstaller::Install(
    const QString& gameDirectory,
    const core::ModRelease& release,
    const QString& archivePath,
    const std::function<bool()>& isCancelled)
{
    Result result;

    if (release.downloadUrl.empty())
    {
        result.error = Tr("the manifest does not provide a download address for this mod");
        return result;
    }

    const QFileInfo archiveInfo(archivePath);

    if (!archiveInfo.isFile() || archiveInfo.size() <= 0)
    {
        result.error = Tr("the downloaded archive is missing");
        return result;
    }

    if (release.size > 0 && archiveInfo.size() != release.size)
    {
        result.error = Tr("the downloaded archive has an unexpected size (%1 instead of %2 bytes)")
                           .arg(archiveInfo.size())
                           .arg(release.size);
        return result;
    }

    if (!release.sha256.empty())
    {
        QString digest;
        QString hashError;

        if (!ComputeSha256(archivePath, digest, hashError))
        {
            result.error = Tr("the downloaded archive could not be verified (%1)").arg(hashError);
            return result;
        }

        if (QString::compare(digest, QString::fromStdString(release.sha256), Qt::CaseInsensitive) != 0)
        {
            result.error = Tr("the downloaded file does not match the checksum published in the manifest");
            return result;
        }
    }

    const fs::path gameDir = ToPath(gameDirectory);
    const fs::path modDirectory = core::ResolveInstallDir(gameDir, release);
    const fs::path staging = StagingPath(modDirectory, release.id);
    const fs::path backup = BackupPath(modDirectory, release.id);

    std::error_code ec;
    std::filesystem::remove_all(staging, ec);
    std::filesystem::create_directories(staging.parent_path(), ec);

    if (ec)
    {
        result.error = Tr("the game folder is not writable (%1)")
                           .arg(FromPath(staging.parent_path()));
        return result;
    }

    // A manifest hint wins; otherwise the archive's single wrapper folder is
    // removed so "<gameDir>/mods/<name>" ends up holding the mod itself.
    fs::path stripPrefix;

    if (!release.archiveRoot.empty())
    {
        stripPrefix = core::Utf8ToPath(release.archiveRoot);
    }
    else
    {
        std::wstring rootError;

        if (!core::FindZipRootFolder(ToPath(archivePath), stripPrefix, rootError))
        {
            result.error = FromWide(rootError);
            return result;
        }
    }

    std::wstring extractError;

    if (!core::ExtractZip(ToPath(archivePath), staging, stripPrefix, isCancelled, extractError))
    {
        std::filesystem::remove_all(staging, ec);
        result.error = FromWide(extractError);
        return result;
    }

    std::filesystem::directory_iterator content(staging, ec);

    if (ec || content == std::filesystem::directory_iterator())
    {
        std::filesystem::remove_all(staging, ec);
        result.error = Tr("the archive did not contain any file");
        return result;
    }

    // Replace the mod folder: move the current one aside, move the new one in,
    // then drop the backup. A failure after the first rename rolls back.
    std::filesystem::remove_all(backup, ec);

    const bool hadPreviousInstall = std::filesystem::exists(modDirectory, ec);

    if (hadPreviousInstall)
    {
        std::filesystem::rename(modDirectory, backup, ec);

        if (ec)
        {
            std::filesystem::remove_all(staging, ec);

            result.error = Tr("the existing mod folder could not be replaced. "
                              "Close Cossacks 3 and make sure the game folder is writable.");
            return result;
        }
    }

    std::filesystem::rename(staging, modDirectory, ec);

    if (ec)
    {
        if (hadPreviousInstall)
        {
            std::error_code rollbackError;
            std::filesystem::rename(backup, modDirectory, rollbackError);
        }

        std::filesystem::remove_all(staging, ec);

        result.error = Tr("the mod folder could not be created in the game directory.");
        return result;
    }

    std::filesystem::remove_all(backup, ec);

    core::InstalledModState state;
    state.id = release.id;
    state.versionNumber = release.versionNumber;
    state.versionLabel = release.versionLabel;
    state.sha256 = release.sha256;
    state.installedAt = ModStateStore::NowIso8601().toUtf8().toStdString();
    state.downloadUrl = release.downloadUrl;

    QStringList warnings;
    QString stateError;

    if (!ModStateStore::Write(FromPath(modDirectory), state, stateError))
    {
        // The files are in place, so this is only a bookkeeping problem: the
        // next check will not know which version is installed.
        warnings << Tr("the installed version could not be recorded (%1)").arg(stateError);
    }

    std::string modListError;

    if (!RegisterInGameMods(gameDir, modDirectory, modListError))
    {
        warnings << Tr("the game's mod list could not be updated (%1)")
                        .arg(QString::fromStdString(modListError));
    }

    if (!warnings.isEmpty())
    {
        result.warning = warnings.join(QStringLiteral("; "));
    }

    result.success = true;
    return result;
}

} // namespace mods

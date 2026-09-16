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
#include "core/ModList.h"
#include "core/ModManifest.h"
#include "core/ModsIni.h"
#include "core/PathUtils.h"
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
    const core::ModListOptions& options,
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
    bool modListChanged = false;

    if (!core::ListInstalledMod(gameDir, modDirectory, options, modListChanged, modListError))
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

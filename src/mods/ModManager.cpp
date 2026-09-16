#include "ModManager.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QtGlobal>

#include "PathBridge.h"
#include "ModInstaller.h"
#include "ModManifestParser.h"
#include "ModStateStore.h"
#include "core/GameDirectory.h"
#include "core/ModList.h"
#include "core/ModManifest.h"
#include "core/Workshop.h"

namespace mods {

namespace {

// Published on the "distribution" branch.
const char kManifestUrl[] =
    "https://raw.githubusercontent.com/aljesco1337/cossacks-mod-launcher/distribution/manifest.json";

// Overrides the manifest address, which is how the integration test points the
// launcher at a local file server.
const char kManifestUrlEnv[] = "CLV_MANIFEST_URL";

const char kSettingsEtag[] = "mods/manifestEtag";
const char kSettingsBody[] = "mods/manifestBody";
const char kSettingsLastCheck[] = "mods/lastCheckUtc";

// The launcher curates a single mod today. Should the manifest ever list more
// than one, this is where the selection would come from.
const char kPreferredModId[] = "ren";

QString Text(const std::string& value)
{
    return QString::fromStdString(value);
}

} // namespace

ModManager::ModManager(QObject* parent)
    : QObject(parent)
    , downloader_(new ModDownloader(this))
    , manifestUrl_(QLatin1String(kManifestUrl))
{
    const QByteArray urlOverride = qgetenv(kManifestUrlEnv);

    if (!urlOverride.isEmpty())
    {
        manifestUrl_ = QString::fromUtf8(urlOverride);
    }

    QSettings settings;
    downloader_->SetManifestUrl(manifestUrl_);
    downloader_->SetManifestETag(settings.value(QLatin1String(kSettingsEtag)).toByteArray());

    connect(downloader_, &ModDownloader::ManifestFetched, this, &ModManager::OnManifestFetched);
    connect(downloader_, &ModDownloader::ManifestNotModified, this, &ModManager::OnManifestNotModified);
    connect(downloader_, &ModDownloader::ArchiveFinished, this, &ModManager::OnArchiveFinished);
    connect(downloader_, &ModDownloader::Failed, this, &ModManager::OnRequestFailed);
    connect(downloader_, &ModDownloader::ArchiveProgress, this, [this](qint64 received, qint64 total)
    {
        downloadPercent_ = total > 0
            ? static_cast<int>((received * 100) / total)
            : -1;

        emit StateChanged();
    });

    LoadCachedManifest();
}

bool ModManager::hasGameFolder() const
{
    return !gameDirectory_.isEmpty() && core::IsValidGameDirectory(gameDirectory_.toStdWString());
}

QString ModManager::GameDirectory() const
{
    return gameDirectory_;
}

void ModManager::SetGameDirectory(const QString& gameDirectory)
{
    const QString trimmed = gameDirectory.trimmed();

    if (trimmed == gameDirectory_)
    {
        return;
    }

    if (stage_ != Stage::None)
    {
        Cancel();
    }

    gameDirectory_ = trimmed;
    RefreshInstalledState();
    emit StateChanged();
}

void ModManager::LoadCachedManifest()
{
    QSettings settings;
    const QByteArray body = settings.value(QLatin1String(kSettingsBody)).toByteArray();

    if (body.isEmpty())
    {
        return;
    }

    QString error;
    const auto manifest = ParseModManifest(body, error);

    if (manifest)
    {
        // Lets the UI show the known version while offline.
        ApplyManifest(*manifest);
    }
}

core::ModListOptions ModManager::BuildModListOptions() const
{
    core::ModListOptions options;

    // The installed mod is switched on by the list layer itself; what is left to
    // decide here is which other mods work together with it, and how the mod is
    // recognised when the workshop provides it.
    if (manifest_)
    {
        options.compatibleMods = manifest_->compatibleMods;
    }

    if (release_)
    {
        options.installedModWorkshopId = release_->workshopId;
    }

    return options;
}

void ModManager::ApplyManifest(const core::ModManifest& manifest)
{
    manifest_ = manifest;
    release_.reset();

    for (const core::ModRelease& candidate : manifest_.value().mods)
    {
        if (candidate.id == kPreferredModId)
        {
            release_ = candidate;
            break;
        }
    }

    if (!release_ && !manifest_.value().mods.empty())
    {
        release_ = manifest_.value().mods.front();
    }

    RefreshInstalledState();
}

void ModManager::RefreshInstalledState()
{
    installed_.reset();
    downloadPercent_ = -1;

    if (!hasGameFolder())
    {
        status_ = Status::NoGameFolder;
        return;
    }

    if (release_)
    {
        // Undo a swap that a previous run left unfinished.
        ModInstaller::Recover(gameDirectory_, *release_);

        installed_ = ModStateStore::Read(ModInstaller::ModDirectory(gameDirectory_, *release_));
    }

    UpdateStatusFromVersions();
}

void ModManager::UpdateStatusFromVersions()
{
    if (!hasGameFolder())
    {
        status_ = Status::NoGameFolder;
        return;
    }

    if (!release_)
    {
        status_ = Status::Idle;
        return;
    }

    if (installed_)
    {
        switch (core::CompareVersions(*release_, *installed_))
        {
        case core::ModUpdateStatus::UpdateAvailable:
            status_ = Status::UpdateAvailable;
            return;

        case core::ModUpdateStatus::UpToDate:
            status_ = Status::UpToDate;
            return;

        case core::ModUpdateStatus::NotInstalled:
        case core::ModUpdateStatus::Unknown:
            break;
        }
    }

    // Nothing recorded, or the record carries no usable version: fall back to
    // what is actually in the mod folder.
    const QDir modDirectory(ModInstaller::ModDirectory(gameDirectory_, *release_));
    const bool folderHasContent = modDirectory.exists() &&
        !modDirectory.entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty();

    if (folderHasContent)
    {
        status_ = Status::InstalledUnknown;
        return;
    }

    // Nothing local: when Steam already provides this mod, a second copy in the
    // game's "mods" folder would only duplicate it.
    if (!release_->workshopId.empty() &&
        core::IsWorkshopItemDownloaded(ToPath(gameDirectory_), release_->workshopId))
    {
        status_ = Status::InstalledFromWorkshop;
        return;
    }

    status_ = Status::NotInstalled;
}

void ModManager::CheckForUpdates(bool userInitiated)
{
    if (isBusy())
    {
        if (userInitiated)
        {
            emit Notification(Tr("an update check or a download is already running"));
        }
        return;
    }

    userInitiatedCheck_ = userInitiated;
    lastError_.clear();
    status_ = Status::Checking;
    emit StateChanged();

    downloader_->FetchManifest();
}

void ModManager::InstallOrUpdate()
{
    if (isBusy() || !release_ || !hasGameFolder())
    {
        return;
    }

    if (status_ == Status::InstalledFromWorkshop)
    {
        // Nothing to download or unpack: Steam's copy is the installation. The
        // game's list is still rebuilt, so a "mods.ini" that was deleted comes
        // back with the workshop items in it.
        const QString name = Text(release_->name);

        const core::ModListOptions options = BuildModListOptions();

        bool changed = false;
        std::string listError;

        if (!core::ListWorkshopMods(ToPath(gameDirectory_), options, changed, listError))
        {
            emit Notification(Tr("the game's mod list could not be updated (%1)")
                                  .arg(QString::fromStdString(listError)));
        }
        else if (changed)
        {
            emit Notification(
                Tr("%1 is already installed through the Steam Workshop; the game's mod list was restored")
                    .arg(name));
        }
        else
        {
            emit Notification(
                Tr("%1 is already installed through the Steam Workshop").arg(name));
        }

        emit StateChanged();
        return;
    }

    if (release_->downloadUrl.empty())
    {
        lastError_ = Tr("the manifest does not provide a download address for this mod");
        emit Notification(Tr("the mod cannot be installed: %1").arg(lastError_));
        emit StateChanged();
        return;
    }

    lastError_.clear();
    downloadPercent_ = -1;
    stage_ = Stage::Downloading;
    emit StateChanged();

    downloader_->DownloadArchive(Text(release_->downloadUrl), TempArchivePath());
}

void ModManager::Cancel()
{
    if (stage_ == Stage::Downloading)
    {
        // The downloader reports back through OnRequestFailed(), which restores
        // the previous status.
        downloader_->Cancel();
    }
}

QString ModManager::TempArchivePath() const
{
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
        QStringLiteral("/CossacksLogViewer");

    const QString name = QStringLiteral("%1-%2.zip")
                             .arg(QString::fromStdString(core::SanitizeFolderName(release_->id, "mod")))
                             .arg(release_->versionNumber);

    return QDir(folder).filePath(name);
}

void ModManager::OnManifestFetched(const QByteArray& body, const QByteArray& etag)
{
    QSettings settings;
    settings.setValue(QLatin1String(kSettingsBody), body);
    settings.setValue(QLatin1String(kSettingsEtag), etag);
    settings.setValue(QLatin1String(kSettingsLastCheck), ModStateStore::NowIso8601());

    downloader_->SetManifestETag(etag);

    QString error;
    const auto manifest = ParseModManifest(body, error);

    if (!manifest)
    {
        HandleCheckFailure(error);
        return;
    }

    lastError_.clear();
    ApplyManifest(*manifest);

    if (status_ == Status::UpdateAvailable && release_)
    {
        emit Notification(Tr("a newer version is available: %1 %2")
                              .arg(Text(release_->name), Text(release_->versionLabel)));
    }
    else if (userInitiatedCheck_)
    {
        emit Notification(Tr("no newer version is available"));
    }

    emit StateChanged();
}

void ModManager::OnManifestNotModified()
{
    QSettings settings;
    settings.setValue(QLatin1String(kSettingsLastCheck), ModStateStore::NowIso8601());

    if (!release_)
    {
        LoadCachedManifest();
    }

    // Re-read the state file: the mod may have been removed since the last run.
    RefreshInstalledState();

    if (userInitiatedCheck_)
    {
        emit Notification(
            status_ == Status::UpdateAvailable
                ? Tr("a newer version is available")
                : Tr("no newer version is available"));
    }

    emit StateChanged();
}

void ModManager::HandleCheckFailure(const QString& error)
{
    lastError_ = error;

    if (release_)
    {
        // Keep showing what the cached manifest said.
        UpdateStatusFromVersions();
    }
    else
    {
        status_ = Status::CheckFailed;
    }

    if (userInitiatedCheck_)
    {
        emit Notification(Tr("the update check failed: %1").arg(error));
    }

    emit StateChanged();
}

void ModManager::OnArchiveFinished(const QString& filePath)
{
    if (!release_ || !hasGameFolder())
    {
        QFile::remove(filePath);
        stage_ = Stage::None;
        UpdateStatusFromVersions();
        emit StateChanged();
        return;
    }

    pendingArchivePath_ = filePath;
    downloadPercent_ = -1;
    stage_ = Stage::Installing;
    emit StateChanged();

    // Unpacking is synchronous, so let the UI paint the new stage first.
    QTimer::singleShot(0, this, [this]() { RunInstall(); });
}

void ModManager::RunInstall()
{
    const QString archivePath = pendingArchivePath_;
    pendingArchivePath_.clear();

    if (archivePath.isEmpty() || !release_ || !hasGameFolder())
    {
        stage_ = Stage::None;
        UpdateStatusFromVersions();
        emit StateChanged();
        return;
    }

    const core::ModRelease release = *release_;
    const core::ModListOptions options = BuildModListOptions();

    const ModInstaller::Result result =
        ModInstaller::Install(gameDirectory_, release, archivePath, options, {});

    QFile::remove(archivePath);

    stage_ = Stage::None;
    installed_ = ModStateStore::Read(ModInstaller::ModDirectory(gameDirectory_, release));
    UpdateStatusFromVersions();

    if (result.success)
    {
        lastError_.clear();

        QString message = Tr("%1 %2 was installed").arg(Text(release.name), Text(release.versionLabel));

        if (!result.warning.isEmpty())
        {
            message += QStringLiteral(" - ") + result.warning;
        }

        emit Notification(message);
    }
    else
    {
        lastError_ = result.error;
        emit Notification(Tr("the mod could not be installed: %1").arg(result.error));
    }

    emit StateChanged();
}

void ModManager::OnRequestFailed(ModDownloader::Request request, const QString& message, bool cancelled)
{
    downloadPercent_ = -1;

    if (request == ModDownloader::Request::Archive)
    {
        stage_ = Stage::None;
    }

    if (cancelled)
    {
        lastError_.clear();
        UpdateStatusFromVersions();
        emit Notification(Tr("the download was cancelled"));
        emit StateChanged();
        return;
    }

    if (request == ModDownloader::Request::Manifest)
    {
        HandleCheckFailure(message);
        return;
    }

    lastError_ = message;
    UpdateStatusFromVersions();
    emit Notification(Tr("the download failed: %1").arg(message));
    emit StateChanged();
}

} // namespace mods

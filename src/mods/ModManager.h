#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <optional>

#include "core/ModList.h"
#include "core/ModManifest.h"
#include "mods/ModDownloader.h"

namespace mods {

// Owns the update state machine: keeps the manifest, knows which version sits
// in the current game folder, and drives download plus installation.
//
// The UI renders from the getters below and never talks to the network or the
// filesystem itself.
class ModManager : public QObject
{
    Q_OBJECT

public:
    // What the launcher currently knows about the mod.
    enum class Status
    {
        NoGameFolder,      // no valid Cossacks 3 folder is selected
        Idle,              // folder known, manifest not fetched yet
        Checking,          // manifest request in flight
        NotInstalled,      // the manifest offers a mod that is not installed
        InstalledUnknown,  // a mod folder exists but its version is unreadable
        InstalledFromWorkshop, // Steam already provides the mod, nothing to install
        UpToDate,
        UpdateAvailable,
        CheckFailed,       // no manifest could be fetched and none is cached
    };

    // The operation that is currently running, independent of the status.
    enum class Stage
    {
        None,
        Downloading,
        Installing,
    };

    explicit ModManager(QObject* parent = nullptr);

    // An empty or invalid path disables the mod features.
    void SetGameDirectory(const QString& gameDirectory);
    QString GameDirectory() const;

    // "userInitiated" makes failures and "nothing new" surface as a
    // notification; background checks stay silent.
    void CheckForUpdates(bool userInitiated);

    void InstallOrUpdate();

    // Only meaningful while a download runs; unpacking cannot be interrupted.
    void Cancel();

    Status status() const { return status_; }
    Stage stage() const { return stage_; }

    bool isBusy() const { return stage_ != Stage::None || status_ == Status::Checking; }
    bool hasGameFolder() const;

    // The mod the launcher manages, or nullptr before a manifest arrived.
    const core::ModRelease* release() const { return release_ ? &*release_ : nullptr; }

    // Version found in the game folder, when it could be determined.
    const core::InstalledModState* installed() const { return installed_ ? &*installed_ : nullptr; }

    // 0..100 while downloading; -1 when the size is unknown or while unpacking.
    int downloadPercent() const { return downloadPercent_; }

    // Technical detail behind the last failure, for tooltips and logs.
    QString lastError() const { return lastError_; }

signals:
    // Emitted whenever any of the getters above changed.
    void StateChanged();

    // One translated line meant for the status bar.
    void Notification(const QString& message);

private:
    void LoadCachedManifest();
    void ApplyManifest(const core::ModManifest& manifest);
    void RefreshInstalledState();
    void UpdateStatusFromVersions();
    void HandleCheckFailure(const QString& error);

    // The mods the repository publishes as compatible with the installed one.
    core::ModListOptions BuildModListOptions() const;

    void OnManifestFetched(const QByteArray& body, const QByteArray& etag);
    void OnManifestNotModified();
    void OnArchiveFinished(const QString& filePath);
    void OnRequestFailed(ModDownloader::Request request, const QString& message, bool cancelled);
    void RunInstall();

    QString TempArchivePath() const;

    ModDownloader* downloader_ = nullptr;

    QString manifestUrl_;
    QString gameDirectory_;

    std::optional<core::ModManifest> manifest_;
    std::optional<core::ModRelease> release_;
    std::optional<core::InstalledModState> installed_;

    Status status_ = Status::NoGameFolder;
    Stage stage_ = Stage::None;

    QString lastError_;
    QString pendingArchivePath_;

    int downloadPercent_ = -1;
    bool userInitiatedCheck_ = false;
};

} // namespace mods

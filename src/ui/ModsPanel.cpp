#include "ModsPanel.h"

#include <QApplication>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>

#include "Theme.h"
#include "core/ModManifest.h"
#include "mods/ModManager.h"

namespace {

constexpr int kProgressBarWidth = 200;

QString Text(const std::string& value)
{
    return QString::fromStdString(value);
}

} // namespace

ModsPanel::ModsPanel(mods::ModManager* manager, QWidget* parent)
    : QFrame(parent)
    , manager_(manager)
{
    setObjectName(QStringLiteral("ModsPanel"));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(12);

    QFont titleFont = font();
    titleFont.setBold(true);

    titleLabel_ = new QLabel(tr("Mods"), this);
    titleLabel_->setFont(titleFont);
    titleLabel_->setMinimumWidth(150);

    statusLabel_ = new QLabel(this);

    progressBar_ = new QProgressBar(this);
    progressBar_->setFixedWidth(kProgressBarWidth);
    progressBar_->setVisible(false);

    actionButton_ = new QPushButton(this);
    actionButton_->setMinimumHeight(32);

    layout->addWidget(titleLabel_);
    layout->addWidget(statusLabel_, 1);
    layout->addWidget(progressBar_);
    layout->addWidget(actionButton_);

    connect(actionButton_, &QPushButton::clicked, this, &ModsPanel::onActionClicked);
    connect(manager_, &mods::ModManager::StateChanged, this, &ModsPanel::refresh);

    applyTheme();
}

void ModsPanel::applyTheme()
{
    const ui::Palette& colors = ui::Colors();

    setStyleSheet(
        QStringLiteral(
            "QFrame#ModsPanel { background:%1; border:1px solid %2; border-radius:10px; }"
            "QLabel { background:transparent; }"
            "QProgressBar { background:%3; border:none; border-radius:6px; "
            "text-align:center; color:%4; }"
            "QProgressBar::chunk { background:%5; border-radius:6px; }")
            .arg(colors.surface, colors.border, colors.progressTrack, colors.text, colors.primary));

    // Re-applies the state colours as well: the status text and the button style
    // depend on the palette.
    refresh();
}

ModsPanel::~ModsPanel()
{
    setWaitCursor(false);
}

void ModsPanel::onActionClicked()
{
    if (manager_->stage() == mods::ModManager::Stage::Downloading)
    {
        manager_->Cancel();
        return;
    }

    manager_->InstallOrUpdate();
}

void ModsPanel::refresh()
{
    const mods::ModManager::Stage stage = manager_->stage();
    const bool downloading = stage == mods::ModManager::Stage::Downloading;
    const bool installing = stage == mods::ModManager::Stage::Installing;

    if (const core::ModRelease* release = manager_->release())
    {
        titleLabel_->setText(Text(release->name));
    }
    else
    {
        titleLabel_->setText(tr("Mods"));
    }

    statusLabel_->setText(statusText());

    // A failure is shown in the warning colour, so it is not mistaken for the
    // "an update is available" message it leaves behind.
    const bool failureVisible = !manager_->lastError().isEmpty() &&
        manager_->status() != mods::ModManager::Status::Checking &&
        manager_->status() != mods::ModManager::Status::CheckFailed;

    statusLabel_->setStyleSheet(
        QStringLiteral("color:%1;")
            .arg(failureVisible ? ui::Colors().accent : ui::Colors().muted));

    const QString error = manager_->lastError();
    statusLabel_->setToolTip(error);
    setToolTip(error);

    const bool enabled = actionEnabled();
    actionButton_->setText(actionText());
    actionButton_->setEnabled(enabled);
    actionButton_->setToolTip(actionToolTip());
    actionButton_->setStyleSheet(
        showsUpdateAccent() ? ui::PrimaryButtonStyle() : ui::NeutralButtonStyle());

    progressBar_->setVisible(downloading || installing);

    if (installing || (downloading && manager_->downloadPercent() < 0))
    {
        // Unknown total: show a busy indicator instead of a fake percentage.
        progressBar_->setRange(0, 0);
    }
    else if (downloading)
    {
        progressBar_->setRange(0, 100);
        progressBar_->setValue(manager_->downloadPercent());
    }

    // Unpacking blocks the event loop, so tell the user why the window stopped
    // responding.
    setWaitCursor(installing);
}

QString ModsPanel::statusText() const
{
    const core::ModRelease* release = manager_->release();
    const core::InstalledModState* installed = manager_->installed();

    const QString installedLabel = installed && !installed->versionLabel.empty()
        ? Text(installed->versionLabel)
        : tr("unknown");

    switch (manager_->stage())
    {
    case mods::ModManager::Stage::Downloading:
        return manager_->downloadPercent() < 0
            ? tr("Downloading the mod...")
            : tr("Downloading the mod... %1%").arg(manager_->downloadPercent());

    case mods::ModManager::Stage::Installing:
        return tr("Installing into the game folder...");

    case mods::ModManager::Stage::None:
        break;
    }

    // Without this the card would simply keep saying that a version is available,
    // which looks exactly like the attempt never happened.
    const QString error = manager_->lastError();

    if (!error.isEmpty() &&
        manager_->status() != mods::ModManager::Status::Checking &&
        manager_->status() != mods::ModManager::Status::CheckFailed)
    {
        return tr("The last attempt failed: %1").arg(error);
    }

    switch (manager_->status())
    {
    case mods::ModManager::Status::NoGameFolder:
        return tr("Select your Cossacks 3 folder to manage mods.");

    case mods::ModManager::Status::Idle:
        return tr("Waiting for the update check...");

    case mods::ModManager::Status::Checking:
        return tr("Checking for updates...");

    case mods::ModManager::Status::CheckFailed:
        return manager_->lastError().isEmpty()
            ? tr("The update check failed.")
            : tr("The update check failed: %1").arg(manager_->lastError());

    case mods::ModManager::Status::NotInstalled:
        return release
            ? tr("Not installed. Version %1 is available.").arg(Text(release->versionLabel))
            : tr("Not installed.");

    case mods::ModManager::Status::InstalledUnknown:
        return tr("Installed, but the version could not be determined.");

    case mods::ModManager::Status::InstalledFromWorkshop:
        return release && !release->workshopId.empty()
            ? tr("Already installed from the Steam Workshop (item %1).")
                  .arg(Text(release->workshopId))
            : tr("Already installed from the Steam Workshop.");

    case mods::ModManager::Status::UpToDate:
        return tr("Installed version %1 - up to date.").arg(installedLabel);

    case mods::ModManager::Status::UpdateAvailable:
        return release
            ? tr("Installed version %1 - version %2 is available.")
                  .arg(installedLabel, Text(release->versionLabel))
            : tr("A newer version is available.");
    }

    return {};
}

QString ModsPanel::actionText() const
{
    const core::ModRelease* release = manager_->release();

    if (manager_->stage() == mods::ModManager::Stage::Downloading)
    {
        return tr("Cancel");
    }

    if (manager_->stage() == mods::ModManager::Stage::Installing)
    {
        return tr("Installing...");
    }

    if (!release)
    {
        return tr("Install");
    }

    // Nothing to install, but the game's list can be rebuilt from the workshop
    // folders that are present.
    if (manager_->status() == mods::ModManager::Status::InstalledFromWorkshop)
    {
        return tr("Restore mod list");
    }

    const QString name = Text(release->name);
    const QString version = Text(release->versionLabel);
    const bool hasVersion = !version.isEmpty();

    // After a failure the same button is the way to try again.
    const bool retry = !manager_->lastError().isEmpty();

    switch (manager_->status())
    {
    case mods::ModManager::Status::UpToDate:
    case mods::ModManager::Status::InstalledUnknown:
        return retry ? tr("Retry reinstalling %1").arg(name) : tr("Reinstall %1").arg(name);

    case mods::ModManager::Status::UpdateAvailable:
        if (!hasVersion)
        {
            return tr("Update %1").arg(name);
        }

        return retry ? tr("Retry update to version %1").arg(version)
                     : tr("Update to version %1").arg(version);

    case mods::ModManager::Status::NotInstalled:
        if (!hasVersion)
        {
            return tr("Install %1").arg(name);
        }

        return retry ? tr("Retry installing %1 %2").arg(name, version)
                     : tr("Install %1 %2").arg(name, version);

    default:
        break;
    }

    return tr("Install");
}

QString ModsPanel::actionToolTip() const
{
    if (!manager_->hasGameFolder())
    {
        return tr("Select a valid Cossacks 3 folder first.");
    }

    if (manager_->status() == mods::ModManager::Status::InstalledFromWorkshop)
    {
        return tr("Steam already provides this mod, so this only writes the workshop "
                  "entries back into the game's mod list.");
    }

    if (manager_->stage() != mods::ModManager::Stage::None)
    {
        return {};
    }

    const core::ModRelease* release = manager_->release();

    if (!release)
    {
        return tr("The mod list has not been downloaded yet.");
    }

    if (release->downloadUrl.empty())
    {
        return tr("The manifest does not provide a download address for %1.").arg(Text(release->name));
    }

    return {};
}

bool ModsPanel::actionEnabled() const
{
    switch (manager_->stage())
    {
    case mods::ModManager::Stage::Downloading:
        return true; // becomes "Cancel"

    case mods::ModManager::Stage::Installing:
        return false;

    case mods::ModManager::Stage::None:
        break;
    }

    if (manager_->status() == mods::ModManager::Status::Checking || !manager_->hasGameFolder())
    {
        return false;
    }

    // Rebuilding the list needs no download address.
    if (manager_->status() == mods::ModManager::Status::InstalledFromWorkshop)
    {
        return true;
    }

    const core::ModRelease* release = manager_->release();

    return release != nullptr && !release->downloadUrl.empty();
}

bool ModsPanel::showsUpdateAccent() const
{
    if (manager_->stage() == mods::ModManager::Stage::Downloading)
    {
        return false;
    }

    return manager_->status() == mods::ModManager::Status::UpdateAvailable ||
        manager_->status() == mods::ModManager::Status::NotInstalled;
}

void ModsPanel::setWaitCursor(bool active)
{
    if (active == waitCursorActive_)
    {
        return;
    }

    if (active)
    {
        QApplication::setOverrideCursor(Qt::WaitCursor);
    }
    else
    {
        QApplication::restoreOverrideCursor();
    }

    waitCursorActive_ = active;
}

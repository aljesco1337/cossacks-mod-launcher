#include "ManageModsDialog.h"

#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include <string>
#include <vector>

#include "Theme.h"
#include "core/ModList.h"
#include "core/ModsIni.h"
#include "mods/PathBridge.h"

namespace {

// The record's "dir" (relative to the game folder) is stored on the name item, so
// switching a checkbox does not have to look it up in the table again.
constexpr int kDirRole = Qt::UserRole;

QString Text(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

} // namespace

ManageModsDialog::ManageModsDialog(const QString& gameDirectory, QWidget* parent)
    : QDialog(parent)
    , gameDirectory_(gameDirectory)
{
    setWindowTitle(tr("Manage mods"));
    setModal(true);
    resize(660, 460);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(12);

    hintLabel_ = new QLabel(
        tr("Every mod the game can load. A switch is written to the file below straight "
           "away, and the game picks it up the next time it starts."),
        this);
    hintLabel_->setWordWrap(true);
    layout->addWidget(hintLabel_);

    pathLabel_ = new QLabel(modsIniPath(), this);
    pathLabel_->setWordWrap(true);
    layout->addWidget(pathLabel_);

    table_ = new QTableWidget(0, 2, this);
    table_->setHorizontalHeaderLabels({ tr("Mod"), tr("Folder") });
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setFocusPolicy(Qt::NoFocus);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(false);
    table_->verticalHeader()->setVisible(false);
    // Row height comes from the header rather than a stylesheet padding rule: with
    // a styled item view Qt draws frames of its own (see applyTheme()).
    table_->verticalHeader()->setDefaultSectionSize(26);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setColumnWidth(0, 260);
    layout->addWidget(table_, 1);

    statusLabel_ = new QLabel(this);
    statusLabel_->setWordWrap(true);

    closeButton_ = new QPushButton(tr("Close"), this);
    closeButton_->setMinimumHeight(32);
    closeButton_->setDefault(true);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(12);
    buttons->addWidget(statusLabel_, 1);
    buttons->addWidget(closeButton_);

    layout->addLayout(buttons);

    connect(closeButton_, &QPushButton::clicked, this, &QDialog::accept);
    connect(table_, &QTableWidget::itemChanged, this, &ManageModsDialog::onItemChanged);

    applyTheme();
    reload();
}

void ManageModsDialog::applyTheme()
{
    const ui::Palette& colors = ui::Colors();

    // The dialog window itself follows the application palette, which
    // MainWindow::applyTheme() sets; only what it draws by hand is coloured here.
    hintLabel_->setStyleSheet(QStringLiteral("color:%1;").arg(colors.text));
    pathLabel_->setStyleSheet(QStringLiteral("color:%1;").arg(colors.muted));
    statusLabel_->setStyleSheet(QStringLiteral("color:%1;").arg(colors.muted));
    closeButton_->setStyleSheet(ui::NeutralButtonStyle());

    // The list is a set of checkboxes, not a list of selectable rows, so the
    // current and hovered items must not be painted as if they were.
    table_->setStyleSheet(
        QStringLiteral(
            "QTableWidget { background:%1; color:%2; border:1px solid %3; }"
            "QTableWidget::item:selected, QTableWidget::item:hover, QTableWidget::item:focus "
            "{ background:%1; color:%2; border:none; }"
            "QHeaderView::section { background:%4; color:%2; border:none; border-bottom:1px solid %3; "
            "padding:6px; }")
            .arg(colors.surface, colors.text, colors.border, colors.background));
}

QString ManageModsDialog::modsIniPath() const
{
    return QDir(gameDirectory_)
        .filePath(QStringLiteral("mods/") + QString::fromLatin1(core::kModsIniFileName));
}

bool ManageModsDialog::reload()
{
    std::vector<core::ModListEntry> entries;
    std::string error;

    if (!core::CollectMods(mods::ToPath(gameDirectory_), entries, error))
    {
        table_->setRowCount(0);
        showStatus(tr("The mod list could not be read: %1").arg(Text(error)), true);
        return false;
    }

    int enabledCount = 0;

    for (const core::ModListEntry& entry : entries)
    {
        if (entry.enabled)
        {
            enabledCount++;
        }
    }

    populating_ = true;
    table_->setUpdatesEnabled(false);
    table_->setRowCount(static_cast<int>(entries.size()));

    for (int index = 0; index < static_cast<int>(entries.size()); index++)
    {
        const core::ModListEntry& entry = entries[index];

        const QString dir = Text(entry.dir);
        const QString tooltip = entry.listed
            ? tr("%1\nListed in mods.ini.").arg(dir)
            : tr("%1\nNot in mods.ini yet. Switching it on adds it.").arg(dir);

        auto* nameItem = new QTableWidgetItem(Text(entry.name));
        nameItem->setFlags(
            (nameItem->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        nameItem->setCheckState(entry.enabled ? Qt::Checked : Qt::Unchecked);
        nameItem->setData(kDirRole, dir);
        nameItem->setToolTip(tooltip);

        // Read-only, and not selectable either: the row is nothing but a checkbox
        // with a description.
        auto* dirItem = new QTableWidgetItem(dir);
        dirItem->setFlags(Qt::ItemIsEnabled);
        dirItem->setToolTip(tooltip);

        table_->setItem(index, 0, nameItem);
        table_->setItem(index, 1, dirItem);
    }

    table_->setUpdatesEnabled(true);
    populating_ = false;

    // Nothing is selected, so the first row must not look as if it were.
    table_->setCurrentItem(nullptr);

    const int count = static_cast<int>(entries.size());

    if (count == 0)
    {
        showStatus(
            tr("No mods found. Install one first, or add a folder to the game's mods folder."),
            false);
    }
    else
    {
        showStatus(
            tr("%1 mod%2 found, %3 switched on.")
                .arg(count)
                .arg(count == 1 ? QStringLiteral("") : QStringLiteral("s"))
                .arg(enabledCount),
            false);
    }

    return true;
}

void ManageModsDialog::onItemChanged(QTableWidgetItem* item)
{
    if (populating_ || item == nullptr || item->column() != 0)
    {
        return;
    }

    const QString dir = item->data(kDirRole).toString();
    const QString name = item->text();
    const bool enabled = item->checkState() == Qt::Checked;

    bool changed = false;
    std::string error;

    if (!core::SetModEnabled(
            mods::ToPath(gameDirectory_),
            dir.toUtf8().toStdString(),
            enabled,
            changed,
            error))
    {
        // Nothing was written, so the box has to go back. Blocked, or the revert
        // would run this handler again.
        {
            const QSignalBlocker blocker(table_);
            item->setCheckState(enabled ? Qt::Unchecked : Qt::Checked);
        }

        showStatus(tr("%1 could not be switched: %2").arg(name, Text(error)), true);
        return;
    }

    if (!changed)
    {
        showStatus(
            tr("%1 stays as it is: mods.ini has no record for it, so switching it off "
               "changes nothing.")
                .arg(name),
            false);
        return;
    }

    changeCount_++;

    showStatus(
        tr("%1 is switched %2. Saved to mods.ini.")
            .arg(name, enabled ? tr("on") : tr("off")),
        false);
}

void ManageModsDialog::showStatus(const QString& message, bool failure)
{
    statusLabel_->setText(message);
    statusLabel_->setStyleSheet(
        QStringLiteral("color:%1;").arg(failure ? ui::Colors().accent : ui::Colors().muted));
}

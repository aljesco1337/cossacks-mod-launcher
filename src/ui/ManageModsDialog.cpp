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

// Whether mods.ini already has a record for the row, which is what decides whether
// the move buttons can act on it.
constexpr int kListedRole = Qt::UserRole + 1;

QString Text(const std::string& value)
{
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}

// The tooltip of both items of a row. A mod without a record moves the way it does
// because a move lists it first (see moveSelected()).
QString RowTooltip(const QString& dir, bool listed)
{
    return listed
        ? ManageModsDialog::tr("%1\nListed in mods.ini.").arg(dir)
        : ManageModsDialog::tr(
              "%1\nNot in mods.ini yet. Switching it on adds it, and moving it "
              "adds its record switched on.")
              .arg(dir);
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
           "away, and the game picks it up the next time it starts. The order of the "
           "list is the order mods.ini keeps the mods in: select a mod and move it up "
           "or down."),
        this);
    hintLabel_->setWordWrap(true);
    layout->addWidget(hintLabel_);

    pathLabel_ = new QLabel(modsIniPath(), this);
    pathLabel_->setWordWrap(true);
    layout->addWidget(pathLabel_);

    table_ = new QTableWidget(0, 2, this);
    table_->setHorizontalHeaderLabels({ tr("Mod"), tr("Folder") });
    // One row at a time: a row is what "Move up"/"Move down" acts on.
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
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

    moveUpButton_ = new QPushButton(tr("Move up"), this);
    moveUpButton_->setMinimumHeight(32);
    moveUpButton_->setToolTip(
        tr("Moves the selected mod one place up in mods.ini."));

    moveDownButton_ = new QPushButton(tr("Move down"), this);
    moveDownButton_->setMinimumHeight(32);
    moveDownButton_->setToolTip(
        tr("Moves the selected mod one place down in mods.ini."));

    closeButton_ = new QPushButton(tr("Close"), this);
    closeButton_->setMinimumHeight(32);
    closeButton_->setDefault(true);

    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(12);
    buttons->addWidget(statusLabel_, 1);
    buttons->addWidget(moveUpButton_);
    buttons->addWidget(moveDownButton_);
    buttons->addWidget(closeButton_);

    layout->addLayout(buttons);

    connect(closeButton_, &QPushButton::clicked, this, &QDialog::accept);
    connect(moveUpButton_, &QPushButton::clicked, this, [this] { moveSelected(true); });
    connect(moveDownButton_, &QPushButton::clicked, this, [this] { moveSelected(false); });
    connect(table_, &QTableWidget::itemChanged, this, &ManageModsDialog::onItemChanged);
    connect(
        table_,
        &QTableWidget::itemSelectionChanged,
        this,
        &ManageModsDialog::updateMoveButtons);

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
    moveUpButton_->setStyleSheet(ui::NeutralButtonStyle());
    moveDownButton_->setStyleSheet(ui::NeutralButtonStyle());

    // The list is a set of checkboxes with one row selected at a time, so hover and
    // focus have to stay quiet (with a stylesheet Qt also paints a frame on the item
    // under the mouse) and the selection is what carries the colour.
    table_->setStyleSheet(
        QStringLiteral(
            "QTableWidget { background:%1; color:%2; border:1px solid %3; }"
            "QTableWidget::item:hover, QTableWidget::item:focus "
            "{ background:%1; color:%2; border:none; }"
            "QTableWidget::item:selected { background:%5; color:%6; border:none; }"
            "QHeaderView::section { background:%4; color:%2; border:none; border-bottom:1px solid %3; "
            "padding:6px; }")
            .arg(
                colors.surface,
                colors.text,
                colors.border,
                colors.background,
                colors.primary,
                colors.primaryText));
}

QString ManageModsDialog::modsIniPath() const
{
    return QDir(gameDirectory_)
        .filePath(QStringLiteral("mods/") + QString::fromLatin1(core::kModsIniFileName));
}

bool ManageModsDialog::reload(const QString& selectDir)
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
        const QString tooltip = RowTooltip(dir, entry.listed);

        auto* nameItem = new QTableWidgetItem(Text(entry.name));
        nameItem->setFlags(
            (nameItem->flags() | Qt::ItemIsUserCheckable) & ~Qt::ItemIsEditable);
        nameItem->setCheckState(entry.enabled ? Qt::Checked : Qt::Unchecked);
        nameItem->setData(kDirRole, dir);
        nameItem->setData(kListedRole, entry.listed);
        nameItem->setToolTip(tooltip);

        // Read-only, but selectable: a click anywhere in the row is what the move
        // buttons act on.
        auto* dirItem = new QTableWidgetItem(dir);
        dirItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        dirItem->setToolTip(tooltip);

        table_->setItem(index, 0, nameItem);
        table_->setItem(index, 1, dirItem);
    }

    table_->setUpdatesEnabled(true);
    populating_ = false;

    if (selectDir.isEmpty())
    {
        // Nothing is selected, so no row may look as if it were, and the move
        // buttons have nothing to act on.
        table_->setCurrentItem(nullptr);
    }
    else
    {
        selectRow(selectDir);
    }

    updateMoveButtons();

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

    // Switching a mod on appends its record at the end of the list, so the row is
    // one the move buttons can act on now. Blocked around the writes, or setting
    // the data would run this handler again.
    if (enabled)
    {
        const QSignalBlocker blocker(table_);

        item->setData(kListedRole, true);
        item->setToolTip(RowTooltip(dir, true));

        if (QTableWidgetItem* dirItem = table_->item(item->row(), 1))
        {
            dirItem->setToolTip(RowTooltip(dir, true));
        }
    }

    updateMoveButtons();

    showStatus(
        tr("%1 is switched %2. Saved to mods.ini.")
            .arg(name, enabled ? tr("on") : tr("off")),
        false);
}

void ManageModsDialog::moveSelected(bool moveUp)
{
    const int row = table_->currentRow();

    if (row < 0)
    {
        return;
    }

    const QTableWidgetItem* item = table_->item(row, 0);

    if (item == nullptr)
    {
        return;
    }

    const QString dir = item->data(kDirRole).toString();
    const QString name = item->text();

    bool changed = false;
    std::string error;

    if (!core::MoveMod(
            mods::ToPath(gameDirectory_),
            dir.toUtf8().toStdString(),
            moveUp,
            changed,
            error))
    {
        showStatus(tr("%1 could not be moved: %2").arg(name, Text(error)), true);
        return;
    }

    if (!changed)
    {
        showStatus(
            tr("%1 is already %2 in the list.").arg(name, moveUp ? tr("first") : tr("last")),
            false);
        return;
    }

    changeCount_++;

    // Read the file back: the new order, the switch of a mod that had to be listed
    // first and the row the buttons act on then all agree with it.
    reload(dir);

    showStatus(
        tr("%1 moved %2. Saved to mods.ini.").arg(name, moveUp ? tr("up") : tr("down")),
        false);
}

void ManageModsDialog::selectRow(const QString& dir)
{
    for (int row = 0; row < table_->rowCount(); row++)
    {
        const QTableWidgetItem* item = table_->item(row, 0);

        if (item != nullptr &&
            item->data(kDirRole).toString().compare(dir, Qt::CaseInsensitive) == 0)
        {
            table_->setCurrentCell(row, 0);
            table_->scrollToItem(item);
            return;
        }
    }

    table_->setCurrentItem(nullptr);
}

void ManageModsDialog::updateMoveButtons()
{
    const int row = table_->currentRow();
    const QTableWidgetItem* item = row < 0 ? nullptr : table_->item(row, 0);

    bool up = false;
    bool down = false;

    if (item != nullptr)
    {
        if (!item->data(kListedRole).toBool())
        {
            // Not in mods.ini yet: a move lists the mod first (switched on) and
            // then places the new record, so both directions have somewhere to go.
            up = true;
            down = true;
        }
        else
        {
            // The records of mods.ini come first and in file order, so the first
            // and the last row carrying a record are the ends of the list.
            int first = -1;
            int last = -1;

            for (int index = 0; index < table_->rowCount(); index++)
            {
                const QTableWidgetItem* candidate = table_->item(index, 0);

                if (candidate != nullptr && candidate->data(kListedRole).toBool())
                {
                    if (first < 0)
                    {
                        first = index;
                    }

                    last = index;
                }
            }

            up = row > first;
            down = row < last;
        }
    }

    moveUpButton_->setEnabled(up);
    moveDownButton_->setEnabled(down);
}

void ManageModsDialog::showStatus(const QString& message, bool failure)
{
    statusLabel_->setText(message);
    statusLabel_->setStyleSheet(
        QStringLiteral("color:%1;").arg(failure ? ui::Colors().accent : ui::Colors().muted));
}

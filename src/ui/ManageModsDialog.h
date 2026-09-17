#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;

// The editor for the game's mod list: every mod the game folder knows - the
// records of "<game folder>/mods/mods.ini", plus the mod folders and workshop
// items the list does not mention yet - with a checkbox each.
//
// A checkbox is written straight through to mods.ini (core::SetModEnabled), so
// there is no separate save step and closing the dialog needs no confirmation. A
// write that fails is reported in the dialog and the box goes back to where it
// was, because the file was left untouched.
class ManageModsDialog : public QDialog
{
    Q_OBJECT

public:
    // "gameDirectory" is the folder whose mods.ini is edited; the caller has
    // already checked that it is a usable game folder.
    explicit ManageModsDialog(const QString& gameDirectory, QWidget* parent = nullptr);

    // Re-applies the colours of the current theme (see ui/Theme.h).
    void applyTheme();

    // How many records were written while the dialog was open, for the caller to
    // report after it closed.
    int changeCount() const { return changeCount_; }

private:
    // Fills the table from mods.ini and the folders next to it. Returns false when
    // the list cannot be read (a UTF-16 mods.ini, for instance).
    bool reload();

    void onItemChanged(QTableWidgetItem* item);
    void showStatus(const QString& message, bool failure);

    QString modsIniPath() const;

    QString gameDirectory_;

    QLabel* hintLabel_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QTableWidget* table_ = nullptr;
    QPushButton* closeButton_ = nullptr;

    // Set while the table is filled: filling it must not look like the user
    // ticking boxes, which would write the file.
    bool populating_ = false;
    int changeCount_ = 0;
};

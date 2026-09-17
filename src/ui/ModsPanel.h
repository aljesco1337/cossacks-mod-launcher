#pragma once

#include <QFrame>

class QLabel;
class QProgressBar;
class QPushButton;

namespace mods {
class ModManager;
}

// Card showing the state of the mod the launcher manages: name, installed
// version, an install/update button and the download progress.
//
// The panel only renders what ModManager reports; all decisions are made there.
class ModsPanel : public QFrame
{
    Q_OBJECT

public:
    explicit ModsPanel(mods::ModManager* manager, QWidget* parent = nullptr);
    ~ModsPanel() override;

    // Re-applies the colours of the current theme (see ui/Theme.h).
    void applyTheme();

private:
    void refresh();
    void onActionClicked();

    QString statusText() const;
    QString actionText() const;
    QString actionToolTip() const;
    bool actionEnabled() const;
    bool showsUpdateAccent() const;

    void setWaitCursor(bool active);

    mods::ModManager* manager_ = nullptr;

    QLabel* titleLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QPushButton* actionButton_ = nullptr;

    bool waitCursorActive_ = false;
};

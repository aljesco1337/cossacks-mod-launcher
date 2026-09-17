#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/Types.h"
#include "Theme.h"

class QComboBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QSplitter;
class QTableWidget;
class QTextEdit;
class QTimer;
class QAction;

class ModsPanel;

namespace mods {
class ModManager;
}

// Cross-platform Qt UI sharing the same core logic on every platform.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // Selecting a game folder and updating the mod is all a player needs; the
    // log viewer is layered on top for troubleshooting.
    enum class ViewMode
    {
        Simple,
        Advanced
    };

    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void browseGame();
    void detectGame();
    void refreshLogs(bool showWarnings = true, bool preserveSelection = false);
    void toggleAutoUpdate(bool enabled);
    void checkModUpdates();
    void toggleAutoModCheck(bool enabled);
    void selectFirstError();
    void showAbout();
    void onGameDirEdited();
    void onLogSelected();
    void onCaretMoved();
    void onHeaderClicked(int section);
    void onLogSettingToggled(bool enabled);
    void exportLogs();
    void revealLastExport();
    void deleteLogs();
    void manageMods();

private:
    void buildUi();
    void buildMenus();
    void loadSettings();
    void saveSettings();

    // Switches between the simple and the advanced view and remembers the
    // choice; applyViewMode() only renders the current one.
    void setViewMode(ViewMode mode);
    void applyViewMode();

    // Switches between the light and the dark theme and remembers the choice;
    // applyTheme() only renders the current one.
    void setThemeMode(ui::ThemeMode mode);
    void applyTheme();

    void showSelectedLog();
    void clearLoadedLogs();
    void updateStatusLabels();
    void updateModifiedColumnTitle();
    void applyPreviewHighlighting();
    bool selectFirstCompileErrorLine();

    // Shows the "a newer version is available" link in the status bar. The
    // update itself is a browser trip: the launcher never replaces its own
    // files, which on Windows are locked and in an AppImage are read-only.
    void showAppUpdate();

    void updateErrorPane();
    void showErrorPane();
    void hideErrorPane();
    bool isCompileErrorLineAtCaret();

    void refreshRecentDirs();
    void addRecentGameDir(const QString& path);

    // Keeps the mod manager in sync with the folder chosen above.
    void syncModGameDirectory();

    // The mod list editor needs a usable game folder, so the button follows the
    // mod card's state.
    void updateModToolsState();

    // Reads "cossacks.ini" and mirrors its logging switches in the checkbox.
    void refreshLogSettings();
    QString cossacksIniPath() const;
    void reportLogSettingFailure(const QString& message);
    void showExportResult(const QString& archivePath, int fileCount);
    void revealInFileManager(const QString& filePath);

    std::optional<QString> detectGameDirectory();
    std::optional<QString> detectSteamGameDirectory();
    std::optional<QString> detectGogGameDirectory();

    // Widgets
    QComboBox* gameDirCombo_ = nullptr;
    QPushButton* browseButton_ = nullptr;
    QPushButton* detectButton_ = nullptr;
    QTableWidget* logTable_ = nullptr;
    QTextEdit* preview_ = nullptr;
    QTextEdit* errorPane_ = nullptr;
    QSplitter* splitter_ = nullptr;
    QLabel* listMetaLabel_ = nullptr;
    QLabel* contentMetaLabel_ = nullptr;
    QLabel* errorCountLabel_ = nullptr;
    QLabel* criticalCountLabel_ = nullptr;

    QTimer* refreshTimer_ = nullptr;
    QAction* autoUpdateAction_ = nullptr;

    QAction* simpleViewAction_ = nullptr;
    QAction* advancedViewAction_ = nullptr;
    QAction* lightThemeAction_ = nullptr;
    QAction* darkThemeAction_ = nullptr;
    QWidget* logSection_ = nullptr;
    QWidget* simpleViewSpacer_ = nullptr;

    QCheckBox* logSettingsCheck_ = nullptr;
    QLabel* logSettingsStatus_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QPushButton* revealExportButton_ = nullptr;
    QPushButton* deleteLogsButton_ = nullptr;
    QLabel* exportResultLabel_ = nullptr;

    mods::ModManager* modManager_ = nullptr;
    ModsPanel* modsPanel_ = nullptr;

    // Edits the mod list the game loads; shared by both views.
    QWidget* modsTools_ = nullptr;
    QPushButton* manageModsButton_ = nullptr;
    QLabel* manageModsHint_ = nullptr;

    QTimer* modCheckTimer_ = nullptr;
    QAction* autoModCheckAction_ = nullptr;
    QLabel* appUpdateLabel_ = nullptr;

    // State
    std::vector<core::LogFileInfo> logFiles_;
    ViewMode viewMode_ = ViewMode::Simple;
    bool autoUpdateLogs_ = true;
    bool autoModCheck_ = true;
    bool refreshing_ = false;
    bool updatingPreview_ = false;
    bool errorPaneVisible_ = false;
    bool sortDescending_ = true;
    int selectedErrorCount_ = 0;
    int selectedCriticalCount_ = 0;

    QStringList recentDirs_;
    int fileColumnWidth_ = 250;
    int modifiedColumnWidth_ = 168;

    // Window size per view, so switching back does not throw away the size the
    // user picked for the other one. The simple view has the folder row, the mod
    // card and the mod list row, which leaves the log viewer's room to the
    // advanced view.
    QSize simpleViewSize_{ 1000, 248 };
    QSize advancedViewSize_{ 1600, 800 };

    QString lastExportPath_;
    bool updatingLogSettings_ = false;

    // Preview cache
    bool previewCacheValid_ = false;
    QString previewFilePath_;
    std::filesystem::file_time_type previewModifiedTime_{};
    std::wstring previewContent_;
};

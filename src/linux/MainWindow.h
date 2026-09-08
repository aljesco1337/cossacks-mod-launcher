#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/Types.h"

class QComboBox;
class QLabel;
class QPushButton;
class QSplitter;
class QTableWidget;
class QTextEdit;
class QTimer;
class QAction;

// Qt-based re-implementation of the Windows UI, sharing the same core logic.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void browseGame();
    void detectGame();
    void refreshLogs(bool showWarnings = true, bool preserveSelection = false);
    void toggleAutoUpdate(bool enabled);
    void selectFirstError();
    void showAbout();
    void onGameDirEdited();
    void onLogSelected();
    void onCaretMoved();
    void onHeaderClicked(int section);

private:
    void buildUi();
    void buildMenus();
    void loadSettings();
    void saveSettings();

    void showSelectedLog();
    void clearLoadedLogs();
    void updateStatusLabels();
    void updateModifiedColumnTitle();
    void applyPreviewHighlighting();
    bool selectFirstCompileErrorLine();

    void updateErrorPane();
    void showErrorPane();
    void hideErrorPane();
    bool isCompileErrorLineAtCaret();

    void refreshRecentDirs();
    void addRecentGameDir(const QString& path);

    std::optional<QString> detectGameDirectory();
    std::optional<QString> detectSteamGameDirectory();

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

    // State
    std::vector<core::LogFileInfo> logFiles_;
    bool autoUpdateLogs_ = true;
    bool refreshing_ = false;
    bool updatingPreview_ = false;
    bool errorPaneVisible_ = false;
    bool sortDescending_ = true;
    int selectedErrorCount_ = 0;
    int selectedCriticalCount_ = 0;

    QStringList recentDirs_;
    int fileColumnWidth_ = 250;
    int modifiedColumnWidth_ = 168;

    // Preview cache
    bool previewCacheValid_ = false;
    QString previewFilePath_;
    std::filesystem::file_time_type previewModifiedTime_{};
    std::wstring previewContent_;
};

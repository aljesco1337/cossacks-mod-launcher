#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include "MainWindow.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPalette>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSizePolicy>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

#include "ManageModsDialog.h"
#include "ModsPanel.h"
#include "Theme.h"
#include "core/GameDirectory.h"
#include "core/GameIni.h"
#include "core/LogModel.h"
#include "core/LogParser.h"
#include "core/ModManifest.h"
#include "core/TextUtils.h"
#include "core/ZipArchive.h"
#include "mods/ModManager.h"
#include "core/Workshop.h"

namespace {

constexpr int kRefreshIntervalMs = 5000;
constexpr int kMaxRecentDirs = 10;

// Mod update checks: shortly after startup and every six hours, so a running
// launcher notices a new release without polling the network hard.
constexpr int kModCheckDelayMs = 1500;
constexpr int kModCheckIntervalMs = 6 * 60 * 60 * 1000;

constexpr int kNotificationTimeoutMs = 8000;

QString fromWide(const std::wstring& value)
{
    return QString::fromStdWString(value);
}

std::wstring toWide(const QString& value)
{
    return value.toStdWString();
}

// "cossacks.ini" is edited as raw bytes, so the file keeps its exact encoding
// and line endings.
bool ReadFileBytes(const QString& filePath, std::string& text, QString& error)
{
    QFile file(filePath);

    if (!file.open(QIODevice::ReadOnly))
    {
        error = file.errorString();
        return false;
    }

    const QByteArray data = file.readAll();
    text.assign(data.constData(), static_cast<std::size_t>(data.size()));

    return true;
}

bool WriteFileBytes(const QString& filePath, const std::string& text, QString& error)
{
    QFile file(filePath);

    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        error = file.errorString();
        return false;
    }

    const qint64 written = file.write(text.data(), static_cast<qint64>(text.size()));

    if (written != static_cast<qint64>(text.size()) || !file.flush())
    {
        error = file.errorString();
        return false;
    }

    file.close();

    return true;
}

#ifdef _WIN32
std::optional<QString> readRegistryString(
    HKEY root,
    const wchar_t* subKey,
    const wchar_t* valueName,
    REGSAM extraFlags = 0)
{
    HKEY key = nullptr;

    LONG result = RegOpenKeyExW(root, subKey, 0, KEY_READ | extraFlags, &key);
    if (result != ERROR_SUCCESS)
    {
        return std::nullopt;
    }

    DWORD type = 0;
    DWORD byteCount = 0;

    result = RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &byteCount);
    if (result != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ))
    {
        RegCloseKey(key);
        return std::nullopt;
    }

    std::wstring value(byteCount / sizeof(wchar_t), L'\0');

    result = RegQueryValueExW(
        key,
        valueName,
        nullptr,
        &type,
        reinterpret_cast<BYTE*>(value.data()),
        &byteCount);

    RegCloseKey(key);

    if (result != ERROR_SUCCESS)
    {
        return std::nullopt;
    }

    while (!value.empty() && value.back() == L'\0')
    {
        value.pop_back();
    }

    if (type == REG_EXPAND_SZ)
    {
        wchar_t expanded[4096]{};
        const DWORD expandedLength = ExpandEnvironmentStringsW(
            value.c_str(),
            expanded,
            4096);

        if (expandedLength > 0 && expandedLength <= 4096)
        {
            value.assign(expanded);
        }
    }

    return value.empty() ? std::nullopt : std::optional<QString>(fromWide(value));
}
#endif

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    // Created first: the panel built below needs it.
    modManager_ = new mods::ModManager(this);

    buildUi();
    buildMenus();

    connect(modManager_, &mods::ModManager::Notification, this, [this](const QString& message)
    {
        statusBar()->showMessage(message, kNotificationTimeoutMs);
    });

    // Connected before the version is known: the cached manifest is read in the
    // manager's constructor, so its first trustworthy verdict - and therefore
    // the signal - arrives while SetApplicationVersionNumber() runs below.
    connect(modManager_, &mods::ModManager::AppUpdateAvailable, this, &MainWindow::showAppUpdate);

    modManager_->SetApplicationVersionNumber(
        core::VersionNumberFromLabel(QCoreApplication::applicationVersion().toStdString()));

    refreshTimer_ = new QTimer(this);
    refreshTimer_->setInterval(kRefreshIntervalMs);
    connect(refreshTimer_, &QTimer::timeout, this, [this]()
    {
        // The log list is off screen in the simple view, so rereading the files
        // every few seconds would only cost disk access.
        if (autoUpdateLogs_ && viewMode_ == ViewMode::Advanced && !isMinimized())
        {
            refreshLogs(false, true);
        }
    });

    modCheckTimer_ = new QTimer(this);
    modCheckTimer_->setInterval(kModCheckIntervalMs);
    connect(modCheckTimer_, &QTimer::timeout, this, [this]()
    {
        modManager_->CheckForUpdates(false);
    });

    loadSettings();

    if (autoUpdateLogs_)
    {
        refreshTimer_->start();
    }

    if (autoModCheck_)
    {
        modCheckTimer_->start();

        // Deferred: the window and the log list come up first, and an
        // unreachable network never delays startup.
        QTimer::singleShot(kModCheckDelayMs, this, [this]()
        {
            modManager_->CheckForUpdates(false);
        });
    }
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("Cossacks Mod Launcher"));
    resize(advancedViewSize_);

    auto* central = new QWidget(this);
    central->setObjectName(QStringLiteral("CentralPane"));

    auto* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(24, 20, 24, 16);
    mainLayout->setSpacing(12);
    setCentralWidget(central);

    // Top bar: folder selector + buttons.
    auto* topBar = new QHBoxLayout();
    topBar->setSpacing(12);

    auto* dirLabel = new QLabel(tr("Game folder:"), central);
    topBar->addWidget(dirLabel);

    gameDirCombo_ = new QComboBox(central);
    gameDirCombo_->setEditable(true);
    gameDirCombo_->setInsertPolicy(QComboBox::NoInsert);
    gameDirCombo_->setMinimumWidth(360);
    topBar->addWidget(gameDirCombo_, 1);

    browseButton_ = new QPushButton(tr("Browse"), central);
    browseButton_->setMinimumHeight(32);
    topBar->addWidget(browseButton_);

    detectButton_ = new QPushButton(tr("Auto-detect"), central);
    detectButton_->setMinimumHeight(32);
    topBar->addWidget(detectButton_);

    mainLayout->addLayout(topBar);

    // Mod status card: installs and updates the mod into the game folder.
    // Shared by both views, so it is never hidden.
    modsPanel_ = new ModsPanel(modManager_, central);
    modsPanel_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    mainLayout->addWidget(modsPanel_);

    // Mod list editor: shared by both views, so the game's mod list can be
    // edited without opening the log viewer.
    modsTools_ = new QWidget(central);

    auto* modsToolsLayout = new QHBoxLayout(modsTools_);
    modsToolsLayout->setContentsMargins(0, 0, 0, 0);
    modsToolsLayout->setSpacing(8);

    manageModsButton_ = new QPushButton(tr("Manage mods..."), modsTools_);
    manageModsButton_->setMinimumHeight(32);
    manageModsButton_->setToolTip(
        tr("Switches the mods the game loads on and off in its mods.ini, and changes "
           "the order it lists them in."));

    manageModsHint_ = new QLabel(
        tr("Choose which mods the game loads, and in which order; the game picks the change "
           "up the next time it starts."),
        modsTools_);

    modsToolsLayout->addWidget(manageModsButton_);
    modsToolsLayout->addWidget(manageModsHint_, 1);

    mainLayout->addWidget(modsTools_);

    // Log area: only shown in the advanced view.
    logSection_ = new QWidget(central);
    auto* logLayout = new QVBoxLayout(logSection_);
    logLayout->setContentsMargins(0, 0, 0, 0);
    logLayout->setSpacing(12);

    // Log tools: switching the engine's logging on and off, and handing the
    // whole log folder to someone else as a single archive.
    auto* logTools = new QHBoxLayout();
    logTools->setSpacing(8);

    logSettingsCheck_ = new QCheckBox(tr("Enable log files"), logSection_);

    logSettingsStatus_ = new QLabel(logSection_);

    exportResultLabel_ = new QLabel(logSection_);

    revealExportButton_ = new QPushButton(tr("Show in folder"), logSection_);
    revealExportButton_->setMinimumHeight(32);
    revealExportButton_->hide();

    exportButton_ = new QPushButton(tr("Export logs..."), logSection_);
    exportButton_->setMinimumHeight(32);
    exportButton_->setToolTip(tr("Compresses everything in the game's log folder into one ZIP file."));

    deleteLogsButton_ = new QPushButton(tr("Delete logs..."), logSection_);
    deleteLogsButton_->setMinimumHeight(32);
    deleteLogsButton_->setToolTip(tr("Removes the log files after a confirmation."));

    logTools->addWidget(logSettingsCheck_);
    logTools->addWidget(logSettingsStatus_);
    logTools->addStretch(1);
    logTools->addWidget(exportResultLabel_);
    logTools->addWidget(revealExportButton_);
    logTools->addSpacing(12);
    logTools->addWidget(deleteLogsButton_);
    logTools->addWidget(exportButton_);

    logLayout->addLayout(logTools);

    // Section labels.
    auto* labels = new QHBoxLayout();
    labels->setSpacing(8);

    QFont sectionFont = font();
    sectionFont.setBold(true);

    auto* logFilesLabel = new QLabel(tr("Log files"), central);
    logFilesLabel->setFont(sectionFont);
    listMetaLabel_ = new QLabel(tr("No logs"), central);

    auto* previewLabel = new QLabel(tr("Preview"), central);
    previewLabel->setFont(sectionFont);
    contentMetaLabel_ = new QLabel(tr("No log selected"), central);

    errorCountLabel_ = new QLabel(tr("Errors: 0"), central);
    criticalCountLabel_ = new QLabel(tr("Critical: 0"), central);

    labels->addWidget(logFilesLabel);
    labels->addWidget(listMetaLabel_);
    labels->addStretch(1);
    labels->addWidget(previewLabel);
    labels->addWidget(contentMetaLabel_);
    labels->addStretch(1);
    labels->addWidget(errorCountLabel_);
    labels->addWidget(criticalCountLabel_);

    logLayout->addLayout(labels);

    // Splitter: log list on the left, preview + error pane on the right.
    splitter_ = new QSplitter(Qt::Horizontal, central);

    logTable_ = new QTableWidget(0, 2, splitter_);
    logTable_->setHorizontalHeaderLabels({ tr("Log file"), tr("Modified") });
    logTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    logTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    logTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    logTable_->verticalHeader()->setVisible(false);
    logTable_->horizontalHeader()->setStretchLastSection(false);
    logTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    logTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);

    auto* rightPane = new QWidget(splitter_);
    auto* rightLayout = new QVBoxLayout(rightPane);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);

    QFont monoFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    monoFont.setPointSize(9);

    preview_ = new QTextEdit(rightPane);
    preview_->setReadOnly(true);
    preview_->setLineWrapMode(QTextEdit::NoWrap);
    preview_->setFont(monoFont);
    rightLayout->addWidget(preview_, 1);

    errorPane_ = new QTextEdit(rightPane);
    errorPane_->setReadOnly(true);
    errorPane_->setLineWrapMode(QTextEdit::NoWrap);
    errorPane_->setFont(monoFont);
    errorPane_->setMaximumHeight(330);
    errorPane_->hide();
    rightLayout->addWidget(errorPane_);

    splitter_->addWidget(logTable_);
    splitter_->addWidget(rightPane);
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 1);
    splitter_->setSizes({ 420, 1180 });

    logLayout->addWidget(splitter_, 1);
    mainLayout->addWidget(logSection_, 1);

    // Absorbs the free space under the mod card while the log area is hidden,
    // so the simple view stays top-aligned instead of drifting to the middle.
    simpleViewSpacer_ = new QWidget(central);
    simpleViewSpacer_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    mainLayout->addWidget(simpleViewSpacer_);

    // Connections.
    connect(browseButton_, &QPushButton::clicked, this, &MainWindow::browseGame);
    connect(detectButton_, &QPushButton::clicked, this, &MainWindow::detectGame);
    connect(manageModsButton_, &QPushButton::clicked, this, &MainWindow::manageMods);
    connect(gameDirCombo_->lineEdit(), &QLineEdit::returnPressed, this, &MainWindow::onGameDirEdited);
    connect(gameDirCombo_, qOverload<int>(&QComboBox::activated), this, [this](int)
    {
        onGameDirEdited();
    });
    connect(logTable_, &QTableWidget::itemSelectionChanged, this, &MainWindow::onLogSelected);
    connect(logTable_->horizontalHeader(), &QHeaderView::sectionClicked, this, &MainWindow::onHeaderClicked);
    connect(preview_, &QTextEdit::cursorPositionChanged, this, &MainWindow::onCaretMoved);
    connect(logSettingsCheck_, &QCheckBox::toggled, this, &MainWindow::onLogSettingToggled);
    connect(exportButton_, &QPushButton::clicked, this, &MainWindow::exportLogs);
    connect(revealExportButton_, &QPushButton::clicked, this, &MainWindow::revealLastExport);
    connect(deleteLogsButton_, &QPushButton::clicked, this, &MainWindow::deleteLogs);

    // A clickable link in the status bar, hidden until the manifest names a
    // newer version. setOpenExternalLinks() makes Qt hand the URL to the
    // browser, so no click handler is needed here.
    appUpdateLabel_ = new QLabel(this);
    appUpdateLabel_->setTextInteractionFlags(Qt::TextBrowserInteraction);
    appUpdateLabel_->setOpenExternalLinks(true);
    appUpdateLabel_->setToolTip(tr("Opens the release page on GitHub."));
    appUpdateLabel_->hide();
    statusBar()->addPermanentWidget(appUpdateLabel_);
}

void MainWindow::buildMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));

    fileMenu->addAction(tr("&Refresh logs"), this, [this]() { refreshLogs(true, true); });

    autoUpdateAction_ = fileMenu->addAction(tr("&Auto-update logs"));
    autoUpdateAction_->setCheckable(true);
    autoUpdateAction_->setChecked(autoUpdateLogs_);
    connect(autoUpdateAction_, &QAction::toggled, this, &MainWindow::toggleAutoUpdate);

    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Browse game folder..."), this, &MainWindow::browseGame);
    fileMenu->addAction(tr("&Detect game folder"), this, &MainWindow::detectGame);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Check for &mod updates"), this, &MainWindow::checkModUpdates);

    autoModCheckAction_ = fileMenu->addAction(tr("Check for mod updates on &startup"));
    autoModCheckAction_->setCheckable(true);
    autoModCheckAction_->setChecked(autoModCheck_);
    connect(autoModCheckAction_, &QAction::toggled, this, &MainWindow::toggleAutoModCheck);

    fileMenu->addSeparator();
    fileMenu->addAction(tr("Select &first compile error"), this, &MainWindow::selectFirstError);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), this, &MainWindow::close);

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));

    auto* viewGroup = new QActionGroup(this);
    viewGroup->setExclusive(true);

    simpleViewAction_ = viewMenu->addAction(tr("&Simple view"));
    simpleViewAction_->setCheckable(true);
    simpleViewAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));
    simpleViewAction_->setToolTip(tr("Game folder and mod updater."));
    viewGroup->addAction(simpleViewAction_);

    advancedViewAction_ = viewMenu->addAction(tr("&Advanced view"));
    advancedViewAction_->setCheckable(true);
    advancedViewAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+2")));
    advancedViewAction_->setToolTip(tr("Adds the log viewer to the simple view."));
    viewGroup->addAction(advancedViewAction_);

    connect(simpleViewAction_, &QAction::triggered, this, [this]()
    {
        setViewMode(ViewMode::Simple);
    });
    connect(advancedViewAction_, &QAction::triggered, this, [this]()
    {
        setViewMode(ViewMode::Advanced);
    });

    addAction(simpleViewAction_);
    addAction(advancedViewAction_);

    viewMenu->addSeparator();

    QMenu* themeMenu = viewMenu->addMenu(tr("&Theme"));

    auto* themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);

    lightThemeAction_ = themeMenu->addAction(tr("&Light"));
    lightThemeAction_->setCheckable(true);
    lightThemeAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+L")));
    themeGroup->addAction(lightThemeAction_);

    darkThemeAction_ = themeMenu->addAction(tr("&Dark"));
    darkThemeAction_->setCheckable(true);
    darkThemeAction_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+D")));
    themeGroup->addAction(darkThemeAction_);

    connect(lightThemeAction_, &QAction::triggered, this, [this]()
    {
        setThemeMode(ui::ThemeMode::Light);
    });
    connect(darkThemeAction_, &QAction::triggered, this, [this]()
    {
        setThemeMode(ui::ThemeMode::Dark);
    });

    addAction(lightThemeAction_);
    addAction(darkThemeAction_);

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("Check for &updates..."), this, &MainWindow::checkModUpdates);
    helpMenu->addAction(tr("&About..."), this, &MainWindow::showAbout);
}

void MainWindow::loadSettings()
{
    QSettings settings;

    autoUpdateLogs_ = settings.value(QStringLiteral("settings/autoUpdateLogs"), true).toBool();
    autoModCheck_ = settings.value(QStringLiteral("mods/autoCheckOnStartup"), true).toBool();
    viewMode_ = settings.value(QStringLiteral("settings/viewMode"), QStringLiteral("simple"))
                        .toString()
                        .compare(QStringLiteral("advanced"), Qt::CaseInsensitive) == 0
        ? ViewMode::Advanced
        : ViewMode::Simple;

    ui::SetThemeMode(ui::ThemeModeFromName(
        settings.value(QStringLiteral("settings/theme"), QStringLiteral("light")).toString()));
    sortDescending_ = settings.value(QStringLiteral("logList/modifiedDescending"), true).toBool();
    fileColumnWidth_ = settings.value(QStringLiteral("logList/fileColumnWidth"), 250).toInt();
    modifiedColumnWidth_ = settings.value(QStringLiteral("logList/modifiedColumnWidth"), 168).toInt();
    recentDirs_ = settings.value(QStringLiteral("recentDirs")).toStringList();

    if (autoUpdateAction_)
    {
        autoUpdateAction_->setChecked(autoUpdateLogs_);
    }

    if (autoModCheckAction_)
    {
        autoModCheckAction_->setChecked(autoModCheck_);
    }

    logTable_->setColumnWidth(0, fileColumnWidth_);
    logTable_->setColumnWidth(1, modifiedColumnWidth_);

    const QByteArray splitterState = settings.value(QStringLiteral("layout/splitterState")).toByteArray();
    if (!splitterState.isEmpty())
    {
        splitter_->restoreState(splitterState);
    }

    const QString saved = settings.value(QStringLiteral("settings/gameDirectory")).toString();
    if (!saved.isEmpty() && core::IsValidGameDirectory(toWide(saved)))
    {
        gameDirCombo_->setCurrentText(fromWide(core::NormalizeGameDirectory(toWide(saved))));
        QTimer::singleShot(0, this, [this]() { refreshLogs(false, false); });
    }
    else
    {
        QTimer::singleShot(0, this, [this]()
        {
            const auto detected = detectGameDirectory();
            if (detected)
            {
                gameDirCombo_->setCurrentText(*detected);
                addRecentGameDir(*detected);
                refreshLogs(false, false);
            }
        });
    }

    refreshRecentDirs();
    updateModifiedColumnTitle();
    applyViewMode();
    applyTheme();
}

void MainWindow::saveSettings()
{
    QSettings settings;

    settings.setValue(QStringLiteral("settings/autoUpdateLogs"), autoUpdateLogs_);
    settings.setValue(QStringLiteral("mods/autoCheckOnStartup"), autoModCheck_);
    settings.setValue(
        QStringLiteral("settings/viewMode"),
        viewMode_ == ViewMode::Advanced ? QStringLiteral("advanced") : QStringLiteral("simple"));
    settings.setValue(QStringLiteral("settings/theme"), ui::ThemeModeName(ui::CurrentThemeMode()));
    settings.setValue(QStringLiteral("settings/gameDirectory"), gameDirCombo_->currentText().trimmed());
    settings.setValue(QStringLiteral("logList/modifiedDescending"), sortDescending_);
    settings.setValue(QStringLiteral("logList/fileColumnWidth"), logTable_->columnWidth(0));
    settings.setValue(QStringLiteral("logList/modifiedColumnWidth"), logTable_->columnWidth(1));
    settings.setValue(QStringLiteral("layout/splitterState"), splitter_->saveState());
    settings.setValue(QStringLiteral("recentDirs"), recentDirs_);
}

void MainWindow::setViewMode(ViewMode mode)
{
    if (viewMode_ == mode)
    {
        return;
    }

    // Remember how the user sized the view being left, so switching back and
    // forth does not resize their window twice.
    (viewMode_ == ViewMode::Advanced ? advancedViewSize_ : simpleViewSize_) = size();

    viewMode_ = mode;
    applyViewMode();
    saveSettings();

    if (mode == ViewMode::Advanced)
    {
        // The list went stale while it was hidden: the refresh timer stands
        // still in the simple view, so catch up now.
        refreshLogs(false, true);
    }
}

void MainWindow::applyViewMode()
{
    const bool advanced = viewMode_ == ViewMode::Advanced;

    // The folder selector, the mod card and the mod list editor are shared by
    // both views; only the log area is exclusive to the advanced one.
    logSection_->setVisible(advanced);
    simpleViewSpacer_->setVisible(!advanced);

    if (simpleViewAction_)
    {
        simpleViewAction_->setChecked(!advanced);
    }

    if (advancedViewAction_)
    {
        advancedViewAction_->setChecked(advanced);
    }

    updateModToolsState();

    // The simple view holds three rows, so it does not need the room the log
    // viewer takes.
    resize(advanced ? advancedViewSize_ : simpleViewSize_);
}

void MainWindow::setThemeMode(ui::ThemeMode mode)
{
    if (ui::CurrentThemeMode() == mode)
    {
        return;
    }

    ui::SetThemeMode(mode);
    applyTheme();
    saveSettings();
}

void MainWindow::applyTheme()
{
    const ui::Palette& colors = ui::Colors();

    if (lightThemeAction_)
    {
        lightThemeAction_->setChecked(ui::CurrentThemeMode() == ui::ThemeMode::Light);
    }

    if (darkThemeAction_)
    {
        darkThemeAction_->setChecked(ui::CurrentThemeMode() == ui::ThemeMode::Dark);
    }

    // Qt's own widgets - table headers, scroll bars, dialogs - follow the
    // application palette.
    qApp->setPalette(ui::ApplicationPalette());

    // The menu bar and its drop-downs are the exception: the native Windows
    // styles draw them with the system theme, so the palette never reaches them
    // and the dark theme ends up with pale text on a pale bar. They are styled
    // by hand here; the menus are children of the bar and inherit its rules.
    menuBar()->setStyleSheet(ui::MenuBarStyle());

    for (QMenu* menu : menuBar()->findChildren<QMenu*>())
    {
        menu->setStyleSheet(ui::MenuStyle());
    }

    // Everything below is styled by hand, so every colour has to be re-applied
    // here; this is the single place that knows how the window looks.
    if (QWidget* central = centralWidget())
    {
        central->setStyleSheet(
            QStringLiteral("QWidget#CentralPane, QWidget#CentralPane QWidget { background:%1; color:%2; }")
                .arg(colors.background, colors.text));
    }

    browseButton_->setStyleSheet(ui::NeutralButtonStyle());
    detectButton_->setStyleSheet(ui::PrimaryButtonStyle());

    logTable_->setStyleSheet(
        QStringLiteral("QTableWidget { background:%1; color:%2; border:1px solid %3; }")
            .arg(colors.surface, colors.text, colors.border));

    preview_->setStyleSheet(
        QStringLiteral("QTextEdit { background:%1; color:%2; border:1px solid %3; }")
            .arg(colors.surface, colors.text, colors.border));

    errorPane_->setStyleSheet(
        QStringLiteral(
            "QTextEdit { background:%1; color:%2; border:1px solid %3; border-left:4px solid %4; }")
            .arg(colors.errorPane, colors.text, colors.border, colors.accent));

    const QString mutedStyle = QStringLiteral("color:%1;").arg(colors.muted);

    listMetaLabel_->setStyleSheet(mutedStyle);
    contentMetaLabel_->setStyleSheet(mutedStyle);
    logSettingsStatus_->setStyleSheet(mutedStyle);
    manageModsHint_->setStyleSheet(mutedStyle);

    exportResultLabel_->setStyleSheet(QStringLiteral("color:%1;").arg(colors.text));

    if (appUpdateLabel_)
    {
        // A rich text label draws its anchors with the link colour of its
        // palette, so that is the only place this colour can be themed. The
        // primary colour, not the accent: accent means "something is wrong",
        // and an update is not.
        QPalette linkPalette = appUpdateLabel_->palette();
        linkPalette.setColor(QPalette::Link, ui::ToColor(colors.primary));
        linkPalette.setColor(QPalette::LinkVisited, ui::ToColor(colors.primary));
        appUpdateLabel_->setPalette(linkPalette);
    }

    revealExportButton_->setStyleSheet(ui::NeutralButtonStyle());
    exportButton_->setStyleSheet(ui::NeutralButtonStyle());
    deleteLogsButton_->setStyleSheet(ui::DangerButtonStyle());
    manageModsButton_->setStyleSheet(ui::NeutralButtonStyle());

    modsPanel_->applyTheme();

    // The preview keeps the character formats of the previous theme inside its
    // document, so the log has to be rendered again.
    previewCacheValid_ = false;

    if (logTable_->currentRow() >= 0)
    {
        showSelectedLog();
    }

    // Also re-colours the counters, which depend on the numbers they show.
    updateStatusLabels();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    // Stops a running download and removes its partial file.
    modManager_->Cancel();

    saveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::browseGame()
{
    const QString directory = QFileDialog::getExistingDirectory(
        this,
        tr("Select Cossacks 3 directory"),
        gameDirCombo_->currentText()
    );

    if (directory.isEmpty())
    {
        return;
    }

    const QString normalized = fromWide(core::NormalizeGameDirectory(toWide(directory)));
    gameDirCombo_->setCurrentText(normalized);

    if (!core::IsValidGameDirectory(toWide(normalized)))
    {
        clearLoadedLogs();
        QMessageBox::warning(
            this,
            tr("Invalid game folder"),
            tr("The selected folder is not a valid Cossacks 3 installation.")
        );
        return;
    }

    addRecentGameDir(normalized);
    saveSettings();
    refreshLogs(false, false);
}

void MainWindow::detectGame()
{
    const auto directory = detectGameDirectory();

    if (!directory)
    {
        clearLoadedLogs();
        QMessageBox::information(
            this,
            tr("Game not found"),
            tr("Cossacks 3 installation was not detected.")
        );
        return;
    }

    addRecentGameDir(*directory);
    gameDirCombo_->setCurrentText(*directory);
    saveSettings();
    refreshLogs(false, false);
}

void MainWindow::onGameDirEdited()
{
    const QString normalized = fromWide(core::NormalizeGameDirectory(toWide(gameDirCombo_->currentText())));
    gameDirCombo_->setCurrentText(normalized);

    if (core::IsValidGameDirectory(toWide(normalized)))
    {
        addRecentGameDir(normalized);
        saveSettings();
        refreshLogs(false, false);
    }
    else
    {
        clearLoadedLogs();
    }
}

void MainWindow::toggleAutoUpdate(bool enabled)
{
    autoUpdateLogs_ = enabled;
    saveSettings();

    if (autoUpdateLogs_)
    {
        refreshTimer_->start();
        refreshLogs(false, true);
    }
    else
    {
        refreshTimer_->stop();
    }
}

void MainWindow::refreshLogs(bool showWarnings, bool preserveSelection)
{
    if (refreshing_)
    {
        return;
    }

    refreshing_ = true;

    QString selectedFileName;

    if (preserveSelection)
    {
        const int selectedIndex = logTable_->currentRow();
        if (selectedIndex >= 0 && selectedIndex < static_cast<int>(logFiles_.size()))
        {
            selectedFileName = fromWide(logFiles_[selectedIndex].fileName);
        }
    }

    const QString baseDirectory = gameDirCombo_->currentText().trimmed();

    if (baseDirectory.isEmpty())
    {
        if (showWarnings)
        {
            QMessageBox::warning(this, tr("Error"), tr("Base directory is empty."));
        }

        refreshing_ = false;
        return;
    }

    syncModGameDirectory();
    refreshLogSettings();

    logFiles_ = core::EnumerateLogFiles(toWide(baseDirectory));
    core::SortLogFiles(logFiles_, sortDescending_);

    logTable_->setUpdatesEnabled(false);
    logTable_->clearContents();
    logTable_->setRowCount(static_cast<int>(logFiles_.size()));

    for (int index = 0; index < static_cast<int>(logFiles_.size()); index++)
    {
        const core::LogFileInfo& logFile = logFiles_[index];

        auto* nameItem = new QTableWidgetItem(fromWide(logFile.fileName));
        auto* modifiedItem = new QTableWidgetItem(fromWide(core::FormatModifiedTime(logFile.modifiedTime)));

        logTable_->setItem(index, 0, nameItem);
        logTable_->setItem(index, 1, modifiedItem);
    }

    logTable_->setUpdatesEnabled(true);

    updateModifiedColumnTitle();
    updateStatusLabels();

    if (!logFiles_.empty())
    {
        int selectedIndex = 0;

        if (!selectedFileName.isEmpty())
        {
            for (int index = 0; index < static_cast<int>(logFiles_.size()); index++)
            {
                if (fromWide(logFiles_[index].fileName) == selectedFileName)
                {
                    selectedIndex = index;
                    break;
                }
            }
        }

        logTable_->selectRow(selectedIndex);
        logTable_->setCurrentCell(selectedIndex, 0);
    }
    else
    {
        clearLoadedLogs();

        if (showWarnings)
        {
            QMessageBox::warning(
                this,
                tr("No logs found"),
                tr("Cannot find log files in:\n%1").arg(baseDirectory + QStringLiteral("/log"))
            );
        }
    }

    refreshing_ = false;

    if (!logFiles_.empty())
    {
        showSelectedLog();
    }
}

void MainWindow::onLogSelected()
{
    if (refreshing_)
    {
        return;
    }

    if (autoUpdateLogs_)
    {
        refreshLogs(false, true);
    }
    else
    {
        showSelectedLog();
    }
}

void MainWindow::showSelectedLog()
{
    const int selectedIndex = logTable_->currentRow();
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(logFiles_.size()))
    {
        return;
    }

    const core::LogFileInfo& logFile = logFiles_[selectedIndex];

    const std::wstring rawContent = core::ReadTextFile(logFile.fullPath);
    const core::LogSeverityCounts severity = core::CountLogSeverity(rawContent);
    selectedErrorCount_ = severity.errors;
    selectedCriticalCount_ = severity.critical;
    const std::wstring content = core::FormatPreviewText(rawContent);

    const bool previewUnchanged =
        previewCacheValid_ &&
        previewFilePath_ == fromWide(logFile.fullPath) &&
        previewModifiedTime_ == logFile.modifiedTime &&
        previewContent_ == content;

    if (previewUnchanged)
    {
        updateStatusLabels();
        return;
    }

    previewCacheValid_ = true;
    previewFilePath_ = fromWide(logFile.fullPath);
    previewModifiedTime_ = logFile.modifiedTime;
    previewContent_ = content;

    updatingPreview_ = true;

    // QTextEdit::setPlainText() inserts the new text using the character format
    // that is active at the start of the existing document. Because the
    // previous log may have been highlighted (red "ERROR" prefix at position
    // zero), that format would otherwise be applied to every character of the
    // newly loaded log. Resetting the document first restores the default
    // format, so applyPreviewHighlighting() only styles real error lines.
    preview_->document()->clear();
    preview_->setPlainText(fromWide(content));
    applyPreviewHighlighting();

    const bool errorFound = selectFirstCompileErrorLine();

    updatingPreview_ = false;

    if (errorFound)
    {
        updateErrorPane();
    }
    else
    {
        hideErrorPane();
    }

    updateStatusLabels();
}

void MainWindow::clearLoadedLogs()
{
    hideErrorPane();

    previewCacheValid_ = false;
    previewFilePath_.clear();
    previewModifiedTime_ = {};
    previewContent_.clear();

    logFiles_.clear();
    selectedErrorCount_ = 0;
    selectedCriticalCount_ = 0;

    logTable_->setRowCount(0);
    preview_->clear();

    updateStatusLabels();
    syncModGameDirectory();
    refreshLogSettings();
}

void MainWindow::updateStatusLabels()
{
    const int fileCount = static_cast<int>(logFiles_.size());

    listMetaLabel_->setText(
        logFiles_.empty()
            ? tr("No logs")
            : tr("%1 file%2").arg(fileCount).arg(fileCount == 1 ? QStringLiteral("") : QStringLiteral("s"))
    );

    const int selectedIndex = logTable_->currentRow();

    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(logFiles_.size()))
    {
        contentMetaLabel_->setText(fromWide(logFiles_[selectedIndex].fileName));
        errorCountLabel_->setText(tr("Errors: %1").arg(selectedErrorCount_));
        criticalCountLabel_->setText(tr("Critical: %1").arg(selectedCriticalCount_));
    }
    else
    {
        contentMetaLabel_->setText(tr("No log selected"));
        errorCountLabel_->setText(tr("Errors: 0"));
        criticalCountLabel_->setText(tr("Critical: 0"));
    }

    const ui::Palette& colors = ui::Colors();

    const QString errorColor = selectedErrorCount_ > 0 ? colors.accentSoft : colors.muted;
    const QString criticalColor = selectedCriticalCount_ > 0 ? colors.accent : colors.muted;

    errorCountLabel_->setStyleSheet(QStringLiteral("color:%1;").arg(errorColor));
    criticalCountLabel_->setStyleSheet(QStringLiteral("color:%1;").arg(criticalColor));
}

void MainWindow::updateModifiedColumnTitle()
{
    QTableWidgetItem* header = logTable_->horizontalHeaderItem(1);
    if (header)
    {
        header->setText(sortDescending_ ? tr("Modified \u25BC") : tr("Modified \u25B2"));
    }
}

void MainWindow::onHeaderClicked(int section)
{
    if (section != 1)
    {
        return;
    }

    sortDescending_ = !sortDescending_;
    saveSettings();
    refreshLogs(false, false);
}

void MainWindow::applyPreviewHighlighting()
{
    QTextDocument* document = preview_->document();

    const ui::Palette& colors = ui::Colors();

    const QString marker = QStringLiteral("CompileFramework() - compile global script error:");

    QTextCharFormat errorLineFormat;
    errorLineFormat.setFontWeight(QFont::Bold);
    errorLineFormat.setForeground(ui::ToColor(colors.logLineText));
    errorLineFormat.setBackground(ui::ToColor(colors.logLine));

    QTextCursor cursor(document);
    cursor.movePosition(QTextCursor::Start);

    while (true)
    {
        cursor = document->find(marker, cursor);
        if (cursor.isNull())
        {
            break;
        }

        QTextCursor lineCursor = cursor;
        lineCursor.movePosition(QTextCursor::StartOfLine);
        lineCursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
        lineCursor.mergeCharFormat(errorLineFormat);

        cursor = lineCursor;
        cursor.movePosition(QTextCursor::EndOfLine);
        cursor.movePosition(QTextCursor::NextCharacter);
    }

    QTextCharFormat errorPrefixFormat;
    errorPrefixFormat.setFontWeight(QFont::Bold);
    errorPrefixFormat.setForeground(ui::ToColor(colors.errorPrefix));

    for (QTextBlock block = document->firstBlock(); block.isValid(); block = block.next())
    {
        if (!block.text().startsWith(QStringLiteral("ERROR")))
        {
            continue;
        }

        QTextCursor selection(document);
        selection.setPosition(block.position());
        selection.setPosition(block.position() + 5, QTextCursor::KeepAnchor);
        selection.mergeCharFormat(errorPrefixFormat);
    }

    // Remove the visible selection left by the search.
    QTextCursor reset(document);
    reset.movePosition(QTextCursor::Start);
    preview_->setTextCursor(reset);
}

bool MainWindow::selectFirstCompileErrorLine()
{
    const QString marker = QStringLiteral("CompileFramework() - compile global script error:");
    QTextCursor cursor = preview_->document()->find(marker, 0);

    if (cursor.isNull())
    {
        return false;
    }

    preview_->setTextCursor(cursor);
    preview_->ensureCursorVisible();
    return true;
}

bool MainWindow::isCompileErrorLineAtCaret()
{
    QTextCursor cursor = preview_->textCursor();
    cursor.select(QTextCursor::LineUnderCursor);
    return cursor.selectedText().contains(
        QStringLiteral("CompileFramework() - compile global script error:")
    );
}

void MainWindow::onCaretMoved()
{
    if (!updatingPreview_)
    {
        updateErrorPane();
    }
}

void MainWindow::updateErrorPane()
{
    if (!isCompileErrorLineAtCaret())
    {
        hideErrorPane();
        return;
    }

    QTextCursor cursor = preview_->textCursor();
    cursor.select(QTextCursor::LineUnderCursor);

    const std::wstring gameDirectory =
        core::NormalizeGameDirectory(toWide(gameDirCombo_->currentText()));

    const std::wstring text = core::ResolveScriptErrorText(
        gameDirectory,
        toWide(cursor.selectedText())
    );

    errorPane_->setPlainText(fromWide(text));
    showErrorPane();
}

void MainWindow::showErrorPane()
{
    if (errorPaneVisible_)
    {
        return;
    }

    errorPaneVisible_ = true;
    errorPane_->show();
}

void MainWindow::hideErrorPane()
{
    if (!errorPaneVisible_)
    {
        return;
    }

    errorPaneVisible_ = false;
    errorPane_->hide();
}

void MainWindow::selectFirstError()
{
    // The log preview is hidden in the simple view; move to the view that can
    // show the result instead of searching an invisible document.
    setViewMode(ViewMode::Advanced);

    if (selectFirstCompileErrorLine())
    {
        updateErrorPane();
    }
    else
    {
        QApplication::beep();
    }
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this,
        tr("About Cossacks Mod Launcher"),
        tr("Cossacks Mod Launcher, Version %1\n\n"
           "A lightweight viewer for Cossacks 3 logs and script compile errors.\n\n"
           "Author: aljesco\n"
           "Contact: aljesco1337@gmail.com")
            .arg(QCoreApplication::applicationVersion())
    );
}

void MainWindow::showAppUpdate()
{
    if (!appUpdateLabel_)
    {
        return;
    }

    const core::AppRelease* release = modManager_->appRelease();

    if (!release)
    {
        appUpdateLabel_->hide();
        return;
    }

    const QString url = QString::fromStdString(release->releasePageUrl);
    const QString version = QString::fromStdString(release->versionLabel);

    appUpdateLabel_->setText(
        tr("<a href=\"%1\">Version %2 is available</a>")
            .arg(url.toHtmlEscaped(), version.toHtmlEscaped()));

    appUpdateLabel_->show();
}

void MainWindow::refreshRecentDirs()
{
    const QString current = gameDirCombo_->currentText();

    gameDirCombo_->blockSignals(true);
    gameDirCombo_->clear();
    gameDirCombo_->addItems(recentDirs_);
    gameDirCombo_->setCurrentText(current);
    gameDirCombo_->blockSignals(false);
}

void MainWindow::addRecentGameDir(const QString& path)
{
    if (!core::IsValidGameDirectory(toWide(path)))
    {
        return;
    }

    const QString normalized = fromWide(core::NormalizeGameDirectory(toWide(path)));

    recentDirs_.removeAll(normalized);
    recentDirs_.prepend(normalized);

    while (recentDirs_.size() > kMaxRecentDirs)
    {
        recentDirs_.removeLast();
    }

    refreshRecentDirs();
}

std::optional<QString> MainWindow::detectSteamGameDirectory()
{
    QStringList roots;

#ifdef _WIN32
    if (const auto steamPath = readRegistryString(
            HKEY_CURRENT_USER,
            L"Software\\Valve\\Steam",
            L"SteamPath"))
    {
        roots << *steamPath;
    }
#else
    const QString home = QDir::homePath();
    roots << home + QStringLiteral("/.local/share/Steam")
          << home + QStringLiteral("/.steam/steam")
          << home + QStringLiteral("/.steam/root")
          << home + QStringLiteral("/.var/app/com.valvesoftware.Steam/.local/share/Steam")
          << home + QStringLiteral("/.var/app/com.valvesoftware.Steam/data/Steam");
#endif

    auto readFile = [](const QString& path) -> QString
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            return {};
        }
        return QString::fromUtf8(file.readAll());
    };

    // "key" is always a fixed literal ("path"/"installdir") with no regex
    // metacharacters, so it can be interpolated directly into the pattern.
    auto stringValues = [](const QString& text, const QString& key) -> QStringList
    {
        const QString pattern = QStringLiteral("\"%1\"\\s+\"([^\"]+)\"").arg(key);
        const QRegularExpression regex(pattern);

        QStringList values;
        auto iterator = regex.globalMatch(text);
        while (iterator.hasNext())
        {
            QString value = iterator.next().captured(1);
            value.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
            values << value;
        }
        return values;
    };

    for (const QString& root : roots)
    {
        if (!QDir(root).exists())
        {
            continue;
        }

        QStringList libraries;
        libraries << root;

        const QString libraryFile = root + QStringLiteral("/steamapps/libraryfolders.vdf");
        if (QFile::exists(libraryFile))
        {
            const QStringList paths = stringValues(readFile(libraryFile), QStringLiteral("path"));
            for (const QString& path : paths)
            {
                if (!path.isEmpty())
                {
                    libraries << path;
                }
            }
        }

        for (const QString& library : libraries)
        {
            const QString manifest = library +
                QStringLiteral("/steamapps/appmanifest_%1.acf")
                    .arg(QLatin1String(core::kCossacksSteamAppId));
            if (!QFile::exists(manifest))
            {
                continue;
            }

            const QStringList installDirs = stringValues(readFile(manifest), QStringLiteral("installdir"));
            if (installDirs.isEmpty())
            {
                continue;
            }

            const QString candidate = library + QStringLiteral("/steamapps/common/") + installDirs.first();
            if (core::IsValidGameDirectory(toWide(candidate)))
            {
                return fromWide(core::NormalizeGameDirectory(toWide(candidate)));
            }
        }
    }

    return std::nullopt;
}

std::optional<QString> MainWindow::detectGogGameDirectory()
{
#ifdef _WIN32
    constexpr wchar_t gogKey[] =
        L"SOFTWARE\\WOW6432Node\\GOG.com\\Games\\1797227701";

    const wchar_t* valueNames[] = {
        L"path",
        L"PATH",
        L"InstallLocation"
    };

    for (const wchar_t* valueName : valueNames)
    {
        if (const auto path = readRegistryString(
                HKEY_LOCAL_MACHINE,
                gogKey,
                valueName,
                KEY_WOW64_32KEY))
        {
            if (core::IsValidGameDirectory(toWide(*path)))
            {
                return fromWide(core::NormalizeGameDirectory(toWide(*path)));
            }
        }
    }
#endif

    return std::nullopt;
}

std::optional<QString> MainWindow::detectGameDirectory()
{
    if (core::IsValidGameDirectory(toWide(gameDirCombo_->currentText())))
    {
        return fromWide(core::NormalizeGameDirectory(toWide(gameDirCombo_->currentText())));
    }

    QSettings settings;
    const QString saved = settings.value(QStringLiteral("settings/gameDirectory")).toString();
    if (!saved.isEmpty() && core::IsValidGameDirectory(toWide(saved)))
    {
        return fromWide(core::NormalizeGameDirectory(toWide(saved)));
    }

    if (const auto steam = detectSteamGameDirectory())
    {
        return steam;
    }

    if (const auto gog = detectGogGameDirectory())
    {
        return gog;
    }

    return std::nullopt;
}

void MainWindow::checkModUpdates()
{
    modManager_->CheckForUpdates(true);
}

void MainWindow::toggleAutoModCheck(bool enabled)
{
    autoModCheck_ = enabled;
    saveSettings();

    if (autoModCheck_)
    {
        modCheckTimer_->start();
        modManager_->CheckForUpdates(false);
    }
    else
    {
        modCheckTimer_->stop();
    }
}

void MainWindow::syncModGameDirectory()
{
    modManager_->SetGameDirectory(gameDirCombo_->currentText().trimmed());

    updateModToolsState();

    // The first time a usable folder is known there is nothing to compare
    // against yet, so look for the manifest right away instead of leaving the
    // panel waiting for the next scheduled check.
    if (modManager_->hasGameFolder() &&
        modManager_->status() == mods::ModManager::Status::Idle)
    {
        modManager_->CheckForUpdates(false);
    }
}

void MainWindow::updateModToolsState()
{
    if (manageModsButton_ == nullptr)
    {
        return;
    }

    // The same rule the mod card uses: without a usable folder there is no mod
    // list to edit (ModManager::hasGameFolder() validates the folder).
    manageModsButton_->setEnabled(modManager_->hasGameFolder());
}

void MainWindow::manageMods()
{
    const QString gameDirectory = gameDirCombo_->currentText().trimmed();

    if (!modManager_->hasGameFolder() || !core::IsValidGameDirectory(toWide(gameDirectory)))
    {
        QMessageBox::warning(
            this,
            tr("No game folder"),
            tr("Select a valid Cossacks 3 folder before editing its mod list."));
        return;
    }

    // The dialog writes every switch straight to mods.ini, so there is nothing to
    // confirm when it closes - only something to report.
    ManageModsDialog dialog(gameDirectory, this);
    dialog.exec();

    if (dialog.changeCount() == 0)
    {
        return;
    }

    statusBar()->showMessage(
        tr("mods.ini updated: %1 change%2. The game has to be started again for them to "
           "take effect.")
            .arg(dialog.changeCount())
            .arg(dialog.changeCount() == 1 ? QStringLiteral("") : QStringLiteral("s")),
        kNotificationTimeoutMs);
}

QString MainWindow::cossacksIniPath() const
{
    const QString gameDirectory = gameDirCombo_->currentText().trimmed();

    if (gameDirectory.isEmpty())
    {
        return QString();
    }

    return QDir(gameDirectory).filePath(QString::fromLatin1(core::kCossacksIniFileName));
}

void MainWindow::refreshLogSettings()
{
    if (!logSettingsCheck_)
    {
        return;
    }

    const QString filePath = cossacksIniPath();

    std::string text;
    QString error;

    const bool readable = !filePath.isEmpty() && ReadFileBytes(filePath, text, error);

    // Setting the checkbox from code must not write the file back.
    updatingLogSettings_ = true;

    if (!readable)
    {
        logSettingsCheck_->setChecked(false);
        logSettingsCheck_->setEnabled(false);
        logSettingsCheck_->setToolTip(
            filePath.isEmpty()
                ? tr("Select a game folder first.")
                : tr("Cannot read %1.").arg(filePath));

        logSettingsStatus_->setText(tr("cossacks.ini not found"));
        updatingLogSettings_ = false;
        return;
    }

    const core::LogIniSettings settings = core::ReadLogSettings(text);
    const bool loggingOn = settings.enabled && settings.root;

    logSettingsCheck_->setEnabled(true);
    logSettingsCheck_->setChecked(loggingOn);
    logSettingsCheck_->setToolTip(
        tr("Switches LogFileEnabled and LogFileRoot in\n%1\nThe new setting applies the next time the game starts.")
            .arg(filePath));

    if (!settings.present)
    {
        logSettingsStatus_->setText(tr("Not configured in cossacks.ini yet"));
    }
    else if (settings.enabled != settings.root)
    {
        // The engine needs both switches, so the checkbox cannot show either.
        logSettingsStatus_->setText(tr("cossacks.ini has only one of the two switches"));
    }
    else
    {
        logSettingsStatus_->setText(loggingOn ? tr("Logging is on") : tr("Logging is off"));
    }

    updatingLogSettings_ = false;
}

void MainWindow::onLogSettingToggled(bool enabled)
{
    if (updatingLogSettings_)
    {
        return;
    }

    const QString filePath = cossacksIniPath();

    if (filePath.isEmpty())
    {
        reportLogSettingFailure(tr("Select a game folder first."));
        return;
    }

    std::string text;
    QString error;

    if (!ReadFileBytes(filePath, text, error))
    {
        reportLogSettingFailure(
            tr("Cannot read %1:\n%2").arg(filePath, error));
        return;
    }

    std::string updated;
    std::string failure;

    if (!core::SetLogSettings(text, enabled, updated, failure))
    {
        reportLogSettingFailure(QString::fromStdString(failure));
        return;
    }

    if (!WriteFileBytes(filePath, updated, error))
    {
        reportLogSettingFailure(tr("Cannot write %1:\n%2").arg(filePath, error));
        return;
    }

    statusBar()->showMessage(
        enabled
            ? tr("Logging enabled in %1").arg(QFileInfo(filePath).fileName())
            : tr("Logging disabled in %1").arg(QFileInfo(filePath).fileName()),
        kNotificationTimeoutMs);

    // Re-read, so the checkbox and its label show what the file really holds.
    refreshLogSettings();
}

void MainWindow::reportLogSettingFailure(const QString& message)
{
    QMessageBox::warning(this, tr("cossacks.ini"), message);

    // Put the checkbox back where the file says it is.
    refreshLogSettings();
}

void MainWindow::exportLogs()
{
    const QString gameDirectory = gameDirCombo_->currentText().trimmed();

    if (gameDirectory.isEmpty() || !core::IsValidGameDirectory(toWide(gameDirectory)))
    {
        QMessageBox::warning(
            this,
            tr("No game folder"),
            tr("Select a valid Cossacks 3 folder first."));
        return;
    }

    const QString logDirectory = QDir(gameDirectory).filePath(QStringLiteral("log"));

    if (!QDir(logDirectory).exists())
    {
        QMessageBox::warning(
            this,
            tr("Nothing to export"),
            tr("The game folder has no \"log\" directory yet.\n\n"
               "Start the game once with log files enabled."));
        return;
    }

    const QDir documents(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
    const QString suggested = documents.filePath(QStringLiteral("cossacks-logs-%1.zip").arg(timestamp));

    const QString target = QFileDialog::getSaveFileName(
        this,
        tr("Export logs"),
        suggested,
        tr("ZIP archive (*.zip)"));

    if (target.isEmpty())
    {
        return;
    }

    std::size_t fileCount = 0;
    std::wstring failure;

    QApplication::setOverrideCursor(Qt::WaitCursor);

    const bool created = core::CreateZipFromFolder(
        std::filesystem::path(toWide(logDirectory)),
        "log",
        std::filesystem::path(toWide(target)),
        fileCount,
        failure);

    QApplication::restoreOverrideCursor();

    if (!created)
    {
        QMessageBox::warning(
            this,
            tr("Export failed"),
            tr("The logs could not be packed:\n%1").arg(fromWide(failure)));
        return;
    }

    showExportResult(target, static_cast<int>(fileCount));

    statusBar()->showMessage(
        tr("Exported %1 log files to %2").arg(static_cast<int>(fileCount)).arg(target),
        kNotificationTimeoutMs);

    revealInFileManager(target);
}

void MainWindow::deleteLogs()
{
    const QString gameDirectory = gameDirCombo_->currentText().trimmed();

    if (gameDirectory.isEmpty() || !core::IsValidGameDirectory(toWide(gameDirectory)))
    {
        QMessageBox::warning(
            this,
            tr("No game folder"),
            tr("Select a valid Cossacks 3 folder first."));
        return;
    }

    const QString logDirectory = QDir(gameDirectory).filePath(QStringLiteral("log"));

    // Enumerated again rather than taken from the list on screen: the game may
    // have written more logs since the last refresh.
    const std::vector<core::LogFileInfo> files = core::EnumerateLogFiles(toWide(gameDirectory));

    if (files.empty())
    {
        QMessageBox::information(
            this,
            tr("Nothing to delete"),
            QDir(logDirectory).exists()
                ? tr("There are no log files in:\n%1").arg(logDirectory)
                : tr("The game folder has no \"log\" directory yet."));
        return;
    }

    qint64 totalBytes = 0;
    QStringList names;

    for (const core::LogFileInfo& file : files)
    {
        totalBytes += QFileInfo(fromWide(file.fullPath)).size();
        names << fromWide(file.fileName);
    }

    const int fileCount = static_cast<int>(files.size());

    QMessageBox confirmation(
        QMessageBox::Warning,
        tr("Delete log files"),
        tr("Delete %1 log file%2 (%3) from:\n%4")
            .arg(fileCount)
            .arg(fileCount == 1 ? QString() : QStringLiteral("s"))
            .arg(QLocale(QLocale::English).formattedDataSize(totalBytes))
            .arg(logDirectory),
        QMessageBox::NoButton,
        this);

    confirmation.setInformativeText(
        tr("This cannot be undone. The game writes new log files the next time it runs."));
    confirmation.setDetailedText(names.join(QLatin1Char('\n')));

    QPushButton* confirmButton = confirmation.addButton(tr("Delete"), QMessageBox::DestructiveRole);
    QPushButton* cancelButton = confirmation.addButton(QMessageBox::Cancel);
    confirmation.setDefaultButton(cancelButton);
    confirmation.setEscapeButton(cancelButton);

    confirmation.exec();

    if (confirmation.clickedButton() != confirmButton)
    {
        return;
    }

    core::LogDeletionResult result;
    std::wstring failure;

    const bool started = core::DeleteLogFiles(toWide(gameDirectory), result, failure);

    // Whatever happened, the list should show what is left.
    refreshLogs(false, false);

    if (!started)
    {
        QMessageBox::warning(this, tr("Delete log files"), fromWide(failure));
        return;
    }

    statusBar()->showMessage(
        tr("Deleted %1 log file%2")
            .arg(result.deleted)
            .arg(result.deleted == 1 ? QString() : QStringLiteral("s")),
        kNotificationTimeoutMs);

    if (!result.failed.empty())
    {
        QStringList left;

        for (const std::wstring& name : result.failed)
        {
            left << fromWide(name);
        }

        const int failedCount = static_cast<int>(result.failed.size());

        // The usual reason is a game that is still running and holding its log
        // open, which is worth saying out loud instead of silently keeping files.
        QMessageBox::warning(
            this,
            tr("Delete log files"),
            tr("%1 file%2 could not be deleted - close the game and try again:\n%3")
                .arg(failedCount)
                .arg(failedCount == 1 ? QString() : QStringLiteral("s"))
                .arg(left.join(QStringLiteral("\n"))));
    }
}

void MainWindow::showExportResult(const QString& archivePath, int fileCount)
{
    lastExportPath_ = archivePath;

    const QFileInfo info(archivePath);

    exportResultLabel_->setText(
        tr("Exported %1 file%2 to %3 (%4)")
            .arg(fileCount)
            .arg(fileCount == 1 ? QString() : QStringLiteral("s"))
            .arg(info.fileName())
            .arg(QLocale(QLocale::English).formattedDataSize(info.size())));

    exportResultLabel_->setToolTip(archivePath);
    revealExportButton_->show();
}

void MainWindow::revealLastExport()
{
    if (!lastExportPath_.isEmpty())
    {
        revealInFileManager(lastExportPath_);
    }
}

void MainWindow::revealInFileManager(const QString& filePath)
{
    const QString nativePath = QDir::toNativeSeparators(filePath);

#if defined(_WIN32)
    QProcess::startDetached(
        QStringLiteral("explorer.exe"),
        { QStringLiteral("/select,") + nativePath });
#elif defined(__APPLE__)
    QProcess::startDetached(QStringLiteral("open"), { QStringLiteral("-R"), nativePath });
#else
    // Linux file managers do not agree on a "select this file" switch, so the
    // folder that holds the archive is opened instead.
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(filePath).absolutePath()));
#endif
}

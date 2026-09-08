#include "MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSplitter>
#include <QTableWidget>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>

#include "core/GameDirectory.h"
#include "core/LogModel.h"
#include "core/LogParser.h"
#include "core/TextUtils.h"

namespace {

constexpr int kRefreshIntervalMs = 5000;
constexpr int kMaxRecentDirs = 10;

const char* kMutedColor = "#64748b";
const char* kTextColor = "#1e293b";
const char* kSurfaceColor = "#ffffff";
const char* kBorderColor = "#d8e1eb";
const char* kBackgroundColor = "#f4f7fb";
const char* kAccentColor = "#c8372e";

QString fromWide(const std::wstring& value)
{
    return QString::fromStdWString(value);
}

std::wstring toWide(const QString& value)
{
    return value.toStdWString();
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    buildUi();
    buildMenus();

    refreshTimer_ = new QTimer(this);
    refreshTimer_->setInterval(kRefreshIntervalMs);
    connect(refreshTimer_, &QTimer::timeout, this, [this]()
    {
        if (autoUpdateLogs_ && !isMinimized())
        {
            refreshLogs(false, true);
        }
    });

    loadSettings();

    if (autoUpdateLogs_)
    {
        refreshTimer_->start();
    }
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("CossacksLogViewer"));
    resize(1600, 800);

    auto* central = new QWidget(this);
    central->setStyleSheet(
        QStringLiteral("QWidget { background: %1; color: %2; }").arg(kBackgroundColor, kTextColor)
    );

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
    browseButton_->setStyleSheet(
        QStringLiteral(
            "QPushButton { background:#f8fafc; color:%1; border:1px solid %2; "
            "border-radius:8px; padding:0 16px; }"
            "QPushButton:hover { background:#eef2f7; }").arg(kTextColor, kBorderColor)
    );
    topBar->addWidget(browseButton_);

    detectButton_ = new QPushButton(tr("Auto-detect"), central);
    detectButton_->setMinimumHeight(32);
    detectButton_->setStyleSheet(
        QStringLiteral(
            "QPushButton { background:#2563eb; color:#ffffff; border:1px solid #2563eb; "
            "border-radius:8px; padding:0 16px; }"
            "QPushButton:hover { background:#1d4ed8; }")
    );
    topBar->addWidget(detectButton_);

    mainLayout->addLayout(topBar);

    // Section labels.
    auto* labels = new QHBoxLayout();
    labels->setSpacing(8);

    QFont sectionFont = font();
    sectionFont.setBold(true);

    auto* logFilesLabel = new QLabel(tr("Log files"), central);
    logFilesLabel->setFont(sectionFont);
    listMetaLabel_ = new QLabel(tr("No logs"), central);
    listMetaLabel_->setStyleSheet(QStringLiteral("color:%1;").arg(kMutedColor));

    auto* previewLabel = new QLabel(tr("Preview"), central);
    previewLabel->setFont(sectionFont);
    contentMetaLabel_ = new QLabel(tr("No log selected"), central);
    contentMetaLabel_->setStyleSheet(QStringLiteral("color:%1;").arg(kMutedColor));

    errorCountLabel_ = new QLabel(tr("Errors: 0"), central);
    errorCountLabel_->setStyleSheet(QStringLiteral("color:%1;").arg(kMutedColor));
    criticalCountLabel_ = new QLabel(tr("Critical: 0"), central);
    criticalCountLabel_->setStyleSheet(QStringLiteral("color:%1;").arg(kMutedColor));

    labels->addWidget(logFilesLabel);
    labels->addWidget(listMetaLabel_);
    labels->addStretch(1);
    labels->addWidget(previewLabel);
    labels->addWidget(contentMetaLabel_);
    labels->addStretch(1);
    labels->addWidget(errorCountLabel_);
    labels->addWidget(criticalCountLabel_);

    mainLayout->addLayout(labels);

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
    logTable_->setStyleSheet(
        QStringLiteral("QTableWidget { background:%1; color:%2; border:1px solid %3; }").arg(kSurfaceColor, kTextColor, kBorderColor)
    );

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
    preview_->setStyleSheet(
        QStringLiteral("QTextEdit { background:%1; color:%2; border:1px solid %3; }").arg(kSurfaceColor, kTextColor, kBorderColor)
    );
    rightLayout->addWidget(preview_, 1);

    errorPane_ = new QTextEdit(rightPane);
    errorPane_->setReadOnly(true);
    errorPane_->setLineWrapMode(QTextEdit::NoWrap);
    errorPane_->setFont(monoFont);
    errorPane_->setMaximumHeight(330);
    errorPane_->setStyleSheet(
        QStringLiteral(
            "QTextEdit { background:#fff7ed; color:%1; border:1px solid %2; border-left:4px solid %2; }")
            .arg(kTextColor, kAccentColor)
    );
    errorPane_->hide();
    rightLayout->addWidget(errorPane_);

    splitter_->addWidget(logTable_);
    splitter_->addWidget(rightPane);
    splitter_->setStretchFactor(0, 0);
    splitter_->setStretchFactor(1, 1);
    splitter_->setSizes({ 420, 1180 });

    mainLayout->addWidget(splitter_, 1);

    // Connections.
    connect(browseButton_, &QPushButton::clicked, this, &MainWindow::browseGame);
    connect(detectButton_, &QPushButton::clicked, this, &MainWindow::detectGame);
    connect(gameDirCombo_->lineEdit(), &QLineEdit::returnPressed, this, &MainWindow::onGameDirEdited);
    connect(gameDirCombo_, qOverload<int>(&QComboBox::activated), this, [this](int)
    {
        onGameDirEdited();
    });
    connect(logTable_, &QTableWidget::itemSelectionChanged, this, &MainWindow::onLogSelected);
    connect(logTable_->horizontalHeader(), &QHeaderView::sectionClicked, this, &MainWindow::onHeaderClicked);
    connect(preview_, &QTextEdit::cursorPositionChanged, this, &MainWindow::onCaretMoved);
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
    fileMenu->addAction(tr("Select &first compile error"), this, &MainWindow::selectFirstError);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("E&xit"), this, &MainWindow::close);

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About..."), this, &MainWindow::showAbout);
}

void MainWindow::loadSettings()
{
    QSettings settings;

    autoUpdateLogs_ = settings.value(QStringLiteral("settings/autoUpdateLogs"), true).toBool();
    sortDescending_ = settings.value(QStringLiteral("logList/modifiedDescending"), true).toBool();
    fileColumnWidth_ = settings.value(QStringLiteral("logList/fileColumnWidth"), 250).toInt();
    modifiedColumnWidth_ = settings.value(QStringLiteral("logList/modifiedColumnWidth"), 168).toInt();
    recentDirs_ = settings.value(QStringLiteral("recentDirs")).toStringList();

    if (autoUpdateAction_)
    {
        autoUpdateAction_->setChecked(autoUpdateLogs_);
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
}

void MainWindow::saveSettings()
{
    QSettings settings;

    settings.setValue(QStringLiteral("settings/autoUpdateLogs"), autoUpdateLogs_);
    settings.setValue(QStringLiteral("settings/gameDirectory"), gameDirCombo_->currentText().trimmed());
    settings.setValue(QStringLiteral("logList/modifiedDescending"), sortDescending_);
    settings.setValue(QStringLiteral("logList/fileColumnWidth"), logTable_->columnWidth(0));
    settings.setValue(QStringLiteral("logList/modifiedColumnWidth"), logTable_->columnWidth(1));
    settings.setValue(QStringLiteral("layout/splitterState"), splitter_->saveState());
    settings.setValue(QStringLiteral("recentDirs"), recentDirs_);
}

void MainWindow::closeEvent(QCloseEvent* event)
{
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

    const QString errorColor = selectedErrorCount_ > 0
        ? QStringLiteral("#b45309")
        : QString::fromLatin1(kMutedColor);
    const QString criticalColor = selectedCriticalCount_ > 0
        ? QString::fromLatin1(kAccentColor)
        : QString::fromLatin1(kMutedColor);

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

    const QString marker = QStringLiteral("CompileFramework() - compile global script error:");

    QTextCharFormat errorLineFormat;
    errorLineFormat.setFontWeight(QFont::Bold);
    errorLineFormat.setForeground(QColor(153, 27, 27));
    errorLineFormat.setBackground(QColor(254, 226, 226));

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
    errorPrefixFormat.setForeground(QColor(220, 38, 38));

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
        tr("About CossacksLogViewer"),
        tr("Cossacks Log Viewer, Version 0.1\n\n"
           "A lightweight viewer for Cossacks 3 logs and script compile errors.\n\n"
           "Author: aljesco\n"
           "Contact: aljesco1337@gmail.com")
    );
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
    const QString home = QDir::homePath();

    const QStringList roots = {
        home + QStringLiteral("/.local/share/Steam"),
        home + QStringLiteral("/.steam/steam"),
        home + QStringLiteral("/.steam/root"),
        home + QStringLiteral("/.var/app/com.valvesoftware.Steam/.local/share/Steam"),
        home + QStringLiteral("/.var/app/com.valvesoftware.Steam/data/Steam"),
    };

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
            const QString manifest = library + QStringLiteral("/steamapps/appmanifest_333420.acf");
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

    return std::nullopt;
}

// CossacksLogViewer.cpp : Defines the entry point for the Windows application.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include "framework.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <shobjidl.h>
#include <richedit.h>
#include <windowsx.h>

#include <vector>
#include <string>
#include <string_view>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <optional>
#include <filesystem>
#include <regex>
#include <cwctype>
#include <iterator>

#include "CossacksLogViewer.h"

#include "core/Types.h"
#include "core/TextUtils.h"
#include "core/LogModel.h"
#include "core/LogParser.h"
#include "core/GameDirectory.h"

namespace fs = std::filesystem;

#pragma comment(lib, "Comctl32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "UxTheme.lib")

#pragma comment(linker, \
    "\"/manifestdependency:type='win32' " \
    "name='Microsoft.Windows.Common-Controls' " \
    "version='6.0.0.0' " \
    "processorArchitecture='*' " \
    "publicKeyToken='6595b64144ccf1df' " \
    "language='*'\"")

#define MAX_LOADSTRING 100
#define IDC_BASE_DIR       1001
#define IDC_BROWSE         1002
#define IDC_DETECT_GAME    1003
#define IDC_LOG_LIST       1004
#define IDC_LOG_CONTENT    1005
#define IDT_LOG_REFRESH    2001

#ifndef FR_DOWN
#define FR_DOWN 0x00000001
#endif

bool gSortDescending = true;
int gLeftPaneWidth = 420;
bool gDraggingSplitter = false;
bool gUpdatingPreview = false;
bool gRefreshingLogFiles = false;
bool gAutoUpdateLogs = true;

constexpr int kSplitterWidth = 6;
constexpr int kLeftPaneMinWidth = 220;
constexpr int kPreviewMinWidth = 420;
constexpr UINT_PTR kLogRefreshTimer = IDT_LOG_REFRESH;
constexpr UINT kLogRefreshIntervalMs = 5000;

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name

HWND hBaseDir;
HWND hBrowseButton;
HWND hDetectButton;
HWND hLogList;
HWND hLogContent;
HWND hDirLabel;
HWND hListLabel;
HWND hContentLabel;
HWND hListMetaLabel;
HWND hContentMetaLabel;
HWND hErrorCountLabel;
HWND hCriticalCountLabel;
HWND hErrorPane = nullptr;
HWND hErrorPaneContent = nullptr;
bool gErrorPaneVisible = false;
int gSelectedErrorCount = 0;
int gSelectedCriticalCount = 0;
bool gPreviewCacheValid = false;
std::wstring gPreviewFilePath;
fs::file_time_type gPreviewModifiedTime{};
std::wstring gPreviewContent;

constexpr int kErrorPaneMaxHeight = 330;

HFONT gUiFont;
HFONT gTitleFont;
HFONT gMonoFont;
HBRUSH gBackgroundBrush;
HBRUSH gSurfaceBrush;
HBRUSH gEditBrush;
HBRUSH gErrorPaneBrush;


std::vector<std::wstring> gRecentGameDirs;

constexpr COLORREF ColorBackground = RGB(244, 247, 251);
constexpr COLORREF ColorSurface = RGB(255, 255, 255);
constexpr COLORREF ColorText = RGB(30, 41, 59);
constexpr COLORREF ColorMutedText = RGB(100, 116, 139);
constexpr COLORREF ColorBorder = RGB(216, 225, 235);
constexpr COLORREF ColorSoftBorder = RGB(232, 238, 246);
constexpr COLORREF ColorPrimary = RGB(37, 99, 235);
constexpr COLORREF ColorPrimaryHot = RGB(29, 78, 216);
constexpr COLORREF ColorButton = RGB(248, 250, 252);
constexpr COLORREF ColorButtonHot = RGB(238, 242, 247);
constexpr COLORREF ColorAccent = RGB(200, 55, 46);
constexpr COLORREF ColorWarningSurface = RGB(255, 247, 237);

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);

using core::LogFileInfo;
using core::LogSeverityCounts;

using core::ReadTextFile;
using core::CountLogSeverity;
using core::FormatPreviewText;
using core::IsValidGameDirectory;
using core::NormalizeGameDirectory;

std::vector<LogFileInfo> gLogFiles;

void LoadLogFiles(HWND owner, bool showWarnings = true, bool preserveSelection = false);
std::wstring GetControlText(HWND control);
void ShowSelectedLog();
void HighlightCompileErrors();
void HighlightErrorPrefixes();
std::optional<std::wstring> SelectDirectory(HWND owner);
void CreateAppFonts();
void ApplyModernControlStyles();
void ApplyRichEditTheme(HWND edit, COLORREF background, COLORREF text);
void LayoutControls(HWND hWnd, int width, int height);
void DrawRoundedPanel(HDC hdc, const RECT& rect, int radius);
void DrawButton(const DRAWITEMSTRUCT* item);
void UpdateStatusLabels();
std::wstring GetCaretLineText(HWND edit);

void CreateErrorPane(HWND owner);
void UpdateErrorPane();
void ShowErrorPane();
void HideErrorPane();
LRESULT CALLBACK ErrorPaneProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

bool IsCompileErrorLineAtCaret();

std::wstring GetIniPath();
WINDOWPLACEMENT LoadWindowPlacement();
void SaveWindowPlacement(HWND window);
void NormalizeWindowPlacement(WINDOWPLACEMENT& placement);

void ClearLoadedLogs();
void ClearPreviewCache();

void SaveGameDirectory(const std::wstring& path);
std::optional<std::wstring> LoadSavedGameDirectory();

std::optional<std::wstring> DetectGameDirectory();
std::optional<std::wstring> DetectSteamGameDirectory();
std::optional<std::wstring> DetectGogGameDirectory();

std::optional<std::wstring> ReadRegistryString(
    HKEY root,
    const wchar_t* subKey,
    const wchar_t* valueName,
    REGSAM extraFlags = 0
);

std::optional<std::wstring> ReadSteamInstallDir(const fs::path& library);

void LoadLogSortSetting();
void SaveLogSortSetting();
void LoadAutoUpdateLogsSetting();
void SaveAutoUpdateLogsSetting();
void UpdateAutoUpdateLogsMenu(HWND hWnd);
void UpdateModifiedColumnTitle();
bool SelectFirstCompileErrorLine();
bool IsOnSplitter(HWND hWnd, int x, int y);

void LoadRecentGameDirs();
void SaveRecentGameDirs();
void RefreshGameDirCombo();
void AddRecentGameDir(const std::wstring& path);

void LoadSplitterSetting();
void SaveSplitterSetting();
void LoadLogColumnSettings();
void SaveLogColumnSettings();


int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    HRESULT comResult = CoInitializeEx(
        nullptr,
        COINIT_APARTMENTTHREADED |
        COINIT_DISABLE_OLE1DDE
    );

    if (FAILED(comResult))
    {
        return 1;
    }

    // TODO: Place code here.
    INITCOMMONCONTROLSEX commonControls{};
    commonControls.dwSize = sizeof(commonControls);
    commonControls.dwICC = ICC_LISTVIEW_CLASSES;

    InitCommonControlsEx(&commonControls);
    LoadLibraryW(L"Msftedit.dll");

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_COSSACKSLOGVIEWER, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    WNDCLASSW popupClass{};
    popupClass.lpfnWndProc = ErrorPaneProc;
    popupClass.lpszClassName = L"CossacksErrorPane";
    popupClass.hInstance = hInstance;
    popupClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    popupClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);

    RegisterClassW(&popupClass);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }


    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_COSSACKSLOGVIEWER));

    MSG msg;

    // Main message loop:
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    CoUninitialize();

    return static_cast<int>(msg.wParam);
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.hbrBackground  = nullptr;
    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_COSSACKSLOGVIEWER));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = nullptr;
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_COSSACKSLOGVIEWER);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;

    WINDOWPLACEMENT placement =
        LoadWindowPlacement();

    const RECT& rect =
        placement.rcNormalPosition;

    HWND hWnd = CreateWindowW(
        szWindowClass,
        szTitle,
        WS_OVERLAPPEDWINDOW,
        rect.left,
        rect.top,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!hWnd)
    {
        return FALSE;
    }

    SetWindowPlacement(
        hWnd,
        &placement
    );

    ShowWindow(
        hWnd,
        placement.showCmd
    );

    UpdateWindow(hWnd);

    return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
    {
        CreateAppFonts();
        LoadLogSortSetting();
        LoadAutoUpdateLogsSetting();
        LoadSplitterSetting();
        LoadLogColumnSettings();
        LoadRecentGameDirs();

        hDirLabel = CreateWindowW(L"STATIC", L"Game folder:", WS_CHILD | WS_VISIBLE, 24, 112, 100, 24, hWnd, nullptr, hInst, nullptr);

        hBaseDir = CreateWindowExW(
            0,
            WC_COMBOBOXW,
            L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWN | CBS_AUTOHSCROLL,
            144, 108, 700, 400,
            hWnd,
            reinterpret_cast<HMENU>(IDC_BASE_DIR),
            hInst,
            nullptr
        );
        RefreshGameDirCombo();

        COMBOBOXINFO comboInfo{};
        comboInfo.cbSize = sizeof(comboInfo);
        GetComboBoxInfo(hBaseDir, &comboInfo);

        SendMessageW(comboInfo.hwndItem, EM_SETREADONLY, TRUE, 0);
        SetWindowTheme(comboInfo.hwndList, L"Explorer", nullptr);

        hBrowseButton = CreateWindowW(
            L"BUTTON",
            L"Browse",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_PUSHBUTTON,
            856, 108, 104, 32,
            hWnd,
            reinterpret_cast<HMENU>(IDC_BROWSE),
            hInst,
            nullptr
        );

        hDetectButton = CreateWindowW(
            L"BUTTON",
            L"Auto-detect",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_PUSHBUTTON,
            0, 0, 160, 32,
            hWnd,
            reinterpret_cast<HMENU>(IDC_DETECT_GAME),
            hInst,
            nullptr
        );

        hListLabel = CreateWindowW(
            L"STATIC",
            L"Log files",
            WS_CHILD | WS_VISIBLE,
            24, 172, 180, 24,
            hWnd,
            nullptr,
            hInst,
            nullptr
        );

        hContentLabel = CreateWindowW(
            L"STATIC",
            L"Preview",
            WS_CHILD | WS_VISIBLE,
            424, 172, 180, 24,
            hWnd,
            nullptr,
            hInst,
            nullptr
        );

        hListMetaLabel = CreateWindowW(
            L"STATIC",
            L"No logs",
            WS_CHILD | WS_VISIBLE | SS_RIGHT,
            204, 172, 180, 24,
            hWnd,
            nullptr,
            hInst,
            nullptr
        );

        hContentMetaLabel = CreateWindowW(
            L"STATIC",
            L"No log selected",
            WS_CHILD | WS_VISIBLE | SS_RIGHT,
            724, 172, 260, 24,
            hWnd,
            nullptr,
            hInst,
            nullptr
        );

        hErrorCountLabel = CreateWindowW(
            L"STATIC",
            L"Errors: 0",
            WS_CHILD | WS_VISIBLE | SS_RIGHT,
            992, 172, 80, 24,
            hWnd,
            nullptr,
            hInst,
            nullptr
        );

        hCriticalCountLabel = CreateWindowW(
            L"STATIC",
            L"Critical: 0",
            WS_CHILD | WS_VISIBLE | SS_RIGHT,
            1080, 172, 112, 24,
            hWnd,
            nullptr,
            hInst,
            nullptr
        );

        hLogList = CreateWindowExW(
            0,
            WC_LISTVIEWW,
            L"",
            WS_CHILD | WS_VISIBLE |
            LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_BORDER,
            24, 202, 360, 500,
            hWnd,
            reinterpret_cast<HMENU>(IDC_LOG_LIST),
            hInst,
            nullptr
        );

        ListView_SetExtendedListViewStyle(
            hLogList,
            LVS_EX_FULLROWSELECT |
            LVS_EX_DOUBLEBUFFER
        );

        LVCOLUMNW column{};
        column.mask = LVCF_TEXT | LVCF_WIDTH;

        column.pszText = const_cast<LPWSTR>(L"Log file");
        column.cx = GetPrivateProfileIntW(L"LogList", L"FileColumnWidth", 250, GetIniPath().c_str());
        ListView_InsertColumn(hLogList, 0, &column);

        column.pszText = const_cast<LPWSTR>(gSortDescending ? L"Modified \u25BC" : L"Modified \u25B2");
        column.cx = GetPrivateProfileIntW(L"LogList", L"ModifiedColumnWidth", 168, GetIniPath().c_str());
        ListView_InsertColumn(hLogList, 1, &column);

        hLogContent = CreateWindowExW(
            0,
            MSFTEDIT_CLASS,
            L"",
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY |
            ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL |
            WS_VSCROLL | WS_HSCROLL | WS_BORDER,
            424, 202, 720, 500,
            hWnd,
            reinterpret_cast<HMENU>(IDC_LOG_CONTENT),
            hInst,
            nullptr
        );

        SetWindowTheme(hLogContent, L"Explorer", nullptr);
        ApplyRichEditTheme(hLogContent, ColorSurface, ColorText);

        SendMessageW(hLogContent, EM_SETEVENTMASK, 0, ENM_SELCHANGE | ENM_SCROLL);

        CreateErrorPane(hWnd);

        HWND controls[] = {
            hDirLabel,
            hBaseDir,
            hBrowseButton,
            hDetectButton,
            hListLabel,
            hContentLabel,
            hListMetaLabel,
            hContentMetaLabel,
            hErrorCountLabel,
            hCriticalCountLabel,
            hLogList,
            hLogContent
        };

        for (HWND control : controls) {
            SendMessageW(
                control,
                WM_SETFONT,
                reinterpret_cast<WPARAM>(gUiFont),
                TRUE
            );
        }

        SendMessageW(hLogContent, WM_SETFONT, reinterpret_cast<WPARAM>(gMonoFont), TRUE);
        ApplyModernControlStyles();
        UpdateStatusLabels();
        UpdateAutoUpdateLogsMenu(hWnd);
        if (gAutoUpdateLogs)
        {
            SetTimer(hWnd, kLogRefreshTimer, kLogRefreshIntervalMs, nullptr);
        }
        
        auto detectedDirectory = DetectGameDirectory();

        if (detectedDirectory)
        {
            SetWindowTextW(
                hBaseDir,
                detectedDirectory->c_str()
            );

            SaveGameDirectory(*detectedDirectory);
            LoadLogFiles(hWnd);
        }
        else
        {
            SetWindowTextW(hBaseDir, L"");
            ClearLoadedLogs();
        }

        return 0;
    }
    case WM_ACTIVATEAPP:
    {
        return 0;
    }
    case WM_MOVE:
    {
        return 0;
    }
    case WM_SIZE:
    {
        if (wParam == SIZE_MINIMIZED) return 0;

        int width = LOWORD(lParam);
        int height = HIWORD(lParam);

        LayoutControls(hWnd, width, height);
        return 0;
    }
    case WM_TIMER:
    {
        if (wParam == kLogRefreshTimer && gAutoUpdateLogs && !IsIconic(hWnd))
        {
            LoadLogFiles(hWnd, false, true);
            return 0;
        }

        break;
    }
    case WM_INITMENUPOPUP:
        UpdateAutoUpdateLogsMenu(hWnd);
        break;
    case WM_CTLCOLORSTATIC:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        HWND control = reinterpret_cast<HWND>(lParam);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, ColorText);

        if (control == hDirLabel ||
            control == hListLabel ||
            control == hContentLabel ||
            control == hListMetaLabel ||
            control == hContentMetaLabel ||
            control == hErrorCountLabel ||
            control == hCriticalCountLabel)
        {
            if (control == hCriticalCountLabel && gSelectedCriticalCount > 0)
            {
                SetTextColor(hdc, ColorAccent);
            }
            else if (control == hErrorCountLabel && gSelectedErrorCount > 0)
            {
                SetTextColor(hdc, RGB(180, 83, 9));
            }
            else if (control == hListMetaLabel ||
                control == hContentMetaLabel ||
                control == hErrorCountLabel ||
                control == hCriticalCountLabel)
            {
                SetTextColor(hdc, ColorMutedText);
            }

            return reinterpret_cast<LRESULT>(gSurfaceBrush);
        }

        return reinterpret_cast<LRESULT>(gBackgroundBrush);
    }
    case WM_CTLCOLOREDIT:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);

        HWND control = reinterpret_cast<HWND>(lParam);
        if (control == hErrorPaneContent)
        {
            SetBkColor(hdc, ColorWarningSurface);
            SetTextColor(hdc, ColorText);
            return reinterpret_cast<LRESULT>(gErrorPaneBrush);
        }

        SetBkColor(hdc, ColorSurface);
        SetTextColor(hdc, ColorText);
        return reinterpret_cast<LRESULT>(gEditBrush);
    }
    case WM_CTLCOLORLISTBOX:
    {
        HDC hdc = reinterpret_cast<HDC>(wParam);
        SetBkColor(hdc, ColorSurface);
        SetTextColor(hdc, ColorText);
        return reinterpret_cast<LRESULT>(gSurfaceBrush);
    }
    case WM_DRAWITEM:
        DrawButton(reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
        return TRUE;
    case WM_NOTIFY:
    {
        LPNMHDR notification = reinterpret_cast<LPNMHDR>(lParam);

        if (notification->idFrom == IDC_LOG_LIST && notification->code == LVN_COLUMNCLICK)
        {
            const NMLISTVIEW* listView = reinterpret_cast<const NMLISTVIEW*>(lParam);

            if (listView->iSubItem == 1)
            {
                gSortDescending = !gSortDescending;
                SaveLogSortSetting();
                LoadLogFiles(hWnd);
                UpdateModifiedColumnTitle();
            }

            return 0;
        }

        // A log file was selected in the left table.
        if (notification->idFrom == IDC_LOG_LIST &&
            notification->code == LVN_ITEMCHANGED)
        {
            LPNMLISTVIEW listView =
                reinterpret_cast<LPNMLISTVIEW>(lParam);

            const bool selectionChanged =
                (listView->uChanged & LVIF_STATE) != 0;

            const bool becameSelected =
                (listView->uNewState & LVIS_SELECTED) != 0;

            if (selectionChanged && becameSelected)
            {
                if (!gRefreshingLogFiles && gAutoUpdateLogs)
                {
                    LoadLogFiles(hWnd, false, true);
                }
                else if (!gRefreshingLogFiles)
                {
                    ShowSelectedLog();
                }
            }

            return 0;
        }

        if (notification->idFrom == IDC_LOG_LIST &&
            (notification->code == NM_CLICK ||
                notification->code == NM_DBLCLK ||
                notification->code == NM_SETFOCUS))
        {
            if (!gRefreshingLogFiles && gAutoUpdateLogs)
            {
                LoadLogFiles(hWnd, false, true);
            }
            else if (!gRefreshingLogFiles)
            {
                ShowSelectedLog();
            }

            return 0;
        }

        if (notification->idFrom == IDC_LOG_CONTENT && notification->code == EN_SELCHANGE)
        {
            if (!gUpdatingPreview) UpdateErrorPane();
            return 0;
        }

        break;
    }
    case WM_COMMAND:
        {
            int wmId = LOWORD(wParam);
            // Parse the menu selections:
            switch (wmId)
            {
            case IDC_BASE_DIR:
            {
                if (HIWORD(wParam) == CBN_SELCHANGE)
                {
                    int index = ComboBox_GetCurSel(hBaseDir);
                    if (index != CB_ERR)
                    {
                        wchar_t buffer[4096]{};
                        ComboBox_GetLBText(hBaseDir, index, buffer);

                        std::wstring selectedPath = NormalizeGameDirectory(buffer);
                        SetWindowTextW(hBaseDir, selectedPath.c_str());

                        if (IsValidGameDirectory(selectedPath))
                        {
                            AddRecentGameDir(selectedPath);
                            RefreshGameDirCombo();
                            LoadLogFiles(hWnd);
                        }
                        else
                        {
                            ClearLoadedLogs();
                        }
                    }
                }

                break;
            }
            case IDM_REFRESH_LOGS:
                LoadLogFiles(hWnd, true, true);
                break;
            case IDM_AUTO_UPDATE_LOGS:
                gAutoUpdateLogs = !gAutoUpdateLogs;
                SaveAutoUpdateLogsSetting();
                UpdateAutoUpdateLogsMenu(hWnd);

                if (gAutoUpdateLogs)
                {
                    SetTimer(hWnd, kLogRefreshTimer, kLogRefreshIntervalMs, nullptr);
                    LoadLogFiles(hWnd, false, true);
                }
                else
                {
                    KillTimer(hWnd, kLogRefreshTimer);
                }
                break;

            case IDC_BROWSE:
            case IDM_BROWSE_GAME:
            {
                std::optional<std::wstring> selectedDirectory = SelectDirectory(hWnd);
                if (!selectedDirectory)
                {
                    break;
                }

                const std::wstring normalizedDirectory = NormalizeGameDirectory(*selectedDirectory);
                SetWindowTextW(hBaseDir, normalizedDirectory.c_str());
                if (!IsValidGameDirectory(normalizedDirectory))
                {
                    ClearLoadedLogs();

                    MessageBoxW(
                        hWnd,
                        L"The selected folder is not a valid Cossacks 3 installation.",
                        L"Invalid game folder",
                        MB_OK | MB_ICONWARNING
                    );

                    break;
                }

                AddRecentGameDir(normalizedDirectory);
                RefreshGameDirCombo();
                SetWindowTextW(hBaseDir, normalizedDirectory.c_str());

                SaveGameDirectory(normalizedDirectory);
                LoadLogFiles(hWnd);
                break;
            }
            case IDC_DETECT_GAME:
            case IDM_DETECT_GAME:
            {
                auto detectedDirectory = DetectGameDirectory();

                if (!detectedDirectory)
                {
                    ClearLoadedLogs();

                    MessageBoxW(
                        hWnd,
                        L"Cossacks 3 installation was not detected.",
                        L"Game not found",
                        MB_OK | MB_ICONINFORMATION
                    );

                    break;
                }

                const std::wstring normalizedDirectory = NormalizeGameDirectory(*detectedDirectory);

                AddRecentGameDir(normalizedDirectory);
                RefreshGameDirCombo();
                SetWindowTextW(hBaseDir, normalizedDirectory.c_str());
                SaveGameDirectory(normalizedDirectory);
                LoadLogFiles(hWnd);

                break;
            }
            case IDM_SELECT_FIRST_ERROR:
                if (SelectFirstCompileErrorLine())
                {
                    UpdateErrorPane();
                }
                else
                {
                    MessageBeep(MB_ICONINFORMATION);
                }
                break;
            case IDM_ABOUT:
                DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
                break;
            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;
            default:
                return DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT client{};
        GetClientRect(hWnd, &client);

        FillRect(hdc, &client, gBackgroundBrush);

        RECT toolbar{ 16, 8, client.right - 16, 64 };
        RECT content{ 16, 74, client.right - 16, client.bottom - 16 };

        DrawRoundedPanel(hdc, toolbar, 12);
        DrawRoundedPanel(hdc, content, 12);

        RECT toolbarLift{ toolbar.left + 10, toolbar.top + 1, toolbar.right - 10, toolbar.top + 2 };
        HBRUSH toolbarLiftBrush = CreateSolidBrush(ColorSoftBorder);
        FillRect(hdc, &toolbarLift, toolbarLiftBrush);
        DeleteObject(toolbarLiftBrush);

        const int splitterX = 24 + gLeftPaneWidth + 8;

        RECT splitterRect{
            splitterX,
            112,
            splitterX + kSplitterWidth,
            client.bottom - 24
        };

        HPEN splitterPen = CreatePen(PS_SOLID, 1, RGB(181, 190, 196));
        HGDIOBJ oldPen = SelectObject(hdc, splitterPen);
        MoveToEx(hdc, splitterX + kSplitterWidth / 2, splitterRect.top + 8, nullptr);
        LineTo(hdc, splitterX + kSplitterWidth / 2, splitterRect.bottom - 8);
        SelectObject(hdc, oldPen);
        DeleteObject(splitterPen);

        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        if (IsOnSplitter(hWnd, x, y))
        {
            gDraggingSplitter = true;
            SetCapture(hWnd);
            return 0;
        }

        break;
    }
    case WM_MOUSEMOVE:
    {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);

        if (gDraggingSplitter)
        {
            RECT client{};
            GetClientRect(hWnd, &client);

            const int clientWidth = static_cast<int>(client.right - client.left);
            const int clientHeight = static_cast<int>(client.bottom - client.top);
            const int margin = 24;

            const int maxLeftWidth = (std::max)(
                kLeftPaneMinWidth,
                clientWidth - margin * 2 - kSplitterWidth - 16 - kPreviewMinWidth
                );

            gLeftPaneWidth = std::clamp(
                x - margin - 8,
                kLeftPaneMinWidth,
                maxLeftWidth
            );

            LayoutControls(hWnd, clientWidth, clientHeight);
            return 0;
        }

        if (IsOnSplitter(hWnd, x, y))
        {
            SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
            return 0;
        }

        break;
    }
    case WM_LBUTTONUP:
    {
        if (gDraggingSplitter)
        {
            gDraggingSplitter = false;
            ReleaseCapture();
            SaveSplitterSetting();
            return 0;
        }

        break;
    }
    case WM_SETCURSOR:
    {
        if (LOWORD(lParam) == HTCLIENT)
        {
            POINT point{};
            GetCursorPos(&point);
            ScreenToClient(hWnd, &point);

            if (IsOnSplitter(hWnd, point.x, point.y))
            {
                SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
                return TRUE;
            }
        }

        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    case WM_DESTROY:
        SaveWindowPlacement(hWnd);
        SaveSplitterSetting();
        SaveLogColumnSettings();
        KillTimer(hWnd, kLogRefreshTimer);

        DeleteObject(gUiFont);
        DeleteObject(gTitleFont);
        DeleteObject(gMonoFont);
        DeleteObject(gBackgroundBrush);
        DeleteObject(gSurfaceBrush);
        DeleteObject(gEditBrush);
        DeleteObject(gErrorPaneBrush);

        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

void CreateAppFonts()
{
    gUiFont = CreateFontW(
        -16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    gTitleFont = CreateFontW(
        -28, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    gMonoFont = CreateFontW(
        -15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Cascadia Mono");

    gBackgroundBrush = CreateSolidBrush(ColorBackground);
    gSurfaceBrush = CreateSolidBrush(ColorSurface);
    gEditBrush = CreateSolidBrush(ColorSurface);
    gErrorPaneBrush = CreateSolidBrush(ColorWarningSurface);
}

void ApplyModernControlStyles()
{
    SetWindowTheme(hLogList, L"Explorer", nullptr);
    SetWindowTheme(hBaseDir, L"Explorer", nullptr);
    SetWindowTheme(hLogContent, L"Explorer", nullptr);

    ListView_SetBkColor(hLogList, ColorSurface);
    ListView_SetTextBkColor(hLogList, ColorSurface);
    ListView_SetTextColor(hLogList, ColorText);
    ListView_SetExtendedListViewStyle(
        hLogList,
        LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_SUBITEMIMAGES
    );
}

void ApplyRichEditTheme(HWND edit, COLORREF background, COLORREF text)
{
    SendMessageW(edit, EM_SETBKGNDCOLOR, 0, background);

    CHARFORMAT2W format{};
    format.cbSize = sizeof(format);
    format.dwMask = CFM_COLOR;
    format.crTextColor = text;

    SendMessageW(
        edit,
        EM_SETCHARFORMAT,
        SCF_DEFAULT,
        reinterpret_cast<LPARAM>(&format)
    );
}

void LayoutControls(HWND hWnd, int width, int height)
{
    const int margin = 24;
    const int gap = 12;
    const int toolbarTop = 20;
    const int controlHeight = 32;
    const int comboDropHeight = 320;
    const int comboTop = toolbarTop + 2;
    const int detectWidth = 132;
    const int browseWidth = 112;
    const int labelsTop = 82;
    const int contentTop = 112;
    const int bottomMargin = 24;

    const int maxLeftWidth = (std::max)(
        kLeftPaneMinWidth,
        width - margin * 2 - kSplitterWidth - 16 - kPreviewMinWidth
        );

    gLeftPaneWidth = std::clamp(
        gLeftPaneWidth,
        kLeftPaneMinWidth,
        maxLeftWidth
    );

    const int splitterLeft = margin + gLeftPaneWidth + 8;
    const int previewLeft = splitterLeft + kSplitterWidth + 8;
    const int contentHeight = (std::max)(
        120,
        height - contentTop - bottomMargin
        );

    MoveWindow(hDirLabel, margin, toolbarTop + 7, 120, 24, TRUE);

    const int editLeft = 156;
    const int detectLeft = width - margin - detectWidth;
    const int browseLeft = detectLeft - gap - browseWidth;
    const int editRight = browseLeft - gap;

    MoveWindow(
        hBaseDir,
        editLeft,
        comboTop,
        (std::max)(180, editRight - editLeft),
        comboDropHeight,
        TRUE
    );

    MoveWindow(
        hBrowseButton,
        browseLeft,
        toolbarTop,
        browseWidth,
        controlHeight,
        TRUE
    );

    MoveWindow(
        hDetectButton,
        detectLeft,
        toolbarTop,
        detectWidth,
        controlHeight,
        TRUE
    );

    MoveWindow(hListLabel, margin, labelsTop, 180, 24, TRUE);
    MoveWindow(hContentLabel, previewLeft, labelsTop, 180, 24, TRUE);
    MoveWindow(hListMetaLabel, margin + 110, labelsTop, (std::max)(80, gLeftPaneWidth - 110), 24, TRUE);

    const int criticalWidth = 116;
    const int errorWidth = 92;
    const int metaGap = 14;
    const int criticalLeft = width - margin - criticalWidth;
    const int errorLeft = criticalLeft - metaGap - errorWidth;
    const int fileMetaLeft = previewLeft + 100;
    const int fileMetaRight = errorLeft - metaGap;

    MoveWindow(hContentMetaLabel, fileMetaLeft, labelsTop, (std::max)(120, fileMetaRight - fileMetaLeft), 24, TRUE);
    MoveWindow(hErrorCountLabel, errorLeft, labelsTop, errorWidth, 24, TRUE);
    MoveWindow(hCriticalCountLabel, criticalLeft, labelsTop, criticalWidth, 24, TRUE);

    MoveWindow(
        hLogList,
        margin,
        contentTop,
        gLeftPaneWidth,
        contentHeight,
        TRUE
    );

    constexpr int scrollbarSize = 18;

    const int errorGap = gErrorPaneVisible ? 8 : 0;
    const int errorHeight = gErrorPaneVisible
        ? (std::min)(kErrorPaneMaxHeight, contentHeight / 2)
        : 0;
    const int previewHeight = contentHeight - errorHeight - errorGap;
    const int previewWidth = width - previewLeft - margin;

    MoveWindow(hLogContent, previewLeft, contentTop, previewWidth, previewHeight, TRUE);

    if (gErrorPaneVisible)
    {
        MoveWindow(hErrorPane, previewLeft, contentTop + previewHeight + errorGap, previewWidth, errorHeight, TRUE);
        ShowWindow(hErrorPane, SW_SHOW);
    }
    else
    {
        ShowWindow(hErrorPane, SW_HIDE);
    }

    InvalidateRect(hWnd, nullptr, TRUE);
}

void DrawRoundedPanel(HDC hdc, const RECT& rect, int radius)
{
    HPEN borderPen = CreatePen(PS_SOLID, 1, ColorBorder);
    HGDIOBJ oldPen = SelectObject(hdc, borderPen);
    HGDIOBJ oldBrush = SelectObject(hdc, gSurfaceBrush);
    RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(borderPen);
}

void DrawButton(const DRAWITEMSTRUCT* item)
{
    bool isPrimary = item->CtlID == IDC_DETECT_GAME;
    bool isPressed = (item->itemState & ODS_SELECTED) != 0;
    bool isDisabled = (item->itemState & ODS_DISABLED) != 0;

    RECT rect = item->rcItem;
    COLORREF fill = isPrimary ? ColorPrimary : ColorButton;
    COLORREF text = isPrimary ? RGB(255, 255, 255) : ColorText;

    if (isPressed)
    {
        fill = isPrimary ? ColorPrimaryHot : ColorButtonHot;
        OffsetRect(&rect, 0, 1);
    }

    if (isDisabled)
    {
        fill = RGB(226, 232, 240);
        text = ColorMutedText;
    }

    HBRUSH buttonBrush = CreateSolidBrush(fill);
    HPEN borderPen = CreatePen(PS_SOLID, 1, isPrimary ? fill : ColorBorder);
    HGDIOBJ oldBrush = SelectObject(item->hDC, buttonBrush);
    HGDIOBJ oldPen = SelectObject(item->hDC, borderPen);
    RoundRect(item->hDC, rect.left, rect.top, rect.right, rect.bottom, 8, 8);

    wchar_t textBuffer[128]{};
    GetWindowTextW(item->hwndItem, textBuffer, ARRAYSIZE(textBuffer));
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, text);
    SelectObject(item->hDC, gUiFont);
    DrawTextW(item->hDC, textBuffer, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SelectObject(item->hDC, oldBrush);
    SelectObject(item->hDC, oldPen);
    DeleteObject(buttonBrush);
    DeleteObject(borderPen);
}

std::wstring GetControlText(HWND control)
{
    int length = GetWindowTextLengthW(control);

    if (length <= 0)
    {
        return {};
    }

    std::wstring text(length + 1, L'\0');

    GetWindowTextW(
        control,
        &text[0],
        length + 1
    );

    text.resize(length);
    return text;
}

void LoadLogFiles(HWND owner, bool showWarnings, bool preserveSelection)
{
    if (gRefreshingLogFiles) return;

    gRefreshingLogFiles = true;

    std::wstring selectedFileName;

    if (preserveSelection)
    {
        int selectedIndex = ListView_GetNextItem(hLogList, -1, LVNI_SELECTED);
        if (selectedIndex >= 0 && selectedIndex < static_cast<int>(gLogFiles.size()))
        {
            selectedFileName = gLogFiles[selectedIndex].fileName;
        }
    }

    std::wstring baseDirectory = GetControlText(hBaseDir);

    if (baseDirectory.empty())
    {
        if (showWarnings)
        {
            MessageBoxW(
                owner,
                L"Base directory is empty.",
                L"Error",
                MB_OK | MB_ICONERROR
            );
        }

        gRefreshingLogFiles = false;
        return;
    }

    std::wstring logsDirectory = baseDirectory + L"\\log";

    gLogFiles = core::EnumerateLogFiles(baseDirectory);
    core::SortLogFiles(gLogFiles, gSortDescending);

    ListView_DeleteAllItems(hLogList);

    if (gLogFiles.empty())
    {
        HideErrorPane();
        ClearPreviewCache();
        gSelectedErrorCount = 0;
        gSelectedCriticalCount = 0;
        SetWindowTextW(hLogContent, L"No log files found.");
        UpdateStatusLabels();

        if (showWarnings)
        {
            MessageBoxW(
                owner,
                (L"Cannot find log files in:\n" + logsDirectory).c_str(),
                L"No logs found",
                MB_OK | MB_ICONWARNING
            );
        }

        gRefreshingLogFiles = false;
        return;
    }

    for (int index = 0;
        index < static_cast<int>(gLogFiles.size());
        index++)
    {
        const LogFileInfo& logFile = gLogFiles[index];

        const std::wstring modified = core::FormatModifiedTime(logFile.modifiedTime);

        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = index;
        item.pszText =
            const_cast<wchar_t*>(logFile.fileName.c_str());

        ListView_InsertItem(hLogList, &item);
        ListView_SetItemText(
            hLogList,
            index,
            1,
            const_cast<wchar_t*>(modified.c_str())
        );
    }

    UpdateModifiedColumnTitle();
    UpdateStatusLabels();

    if (!gLogFiles.empty())
    {
        int selectedIndex = 0;

        if (!selectedFileName.empty())
        {
            for (int index = 0; index < static_cast<int>(gLogFiles.size()); index++)
            {
                if (gLogFiles[index].fileName == selectedFileName)
                {
                    selectedIndex = index;
                    break;
                }
            }
        }

        ListView_SetItemState(
            hLogList,
            selectedIndex,
            LVIS_SELECTED | LVIS_FOCUSED,
            LVIS_SELECTED | LVIS_FOCUSED
        );

        ListView_EnsureVisible(hLogList, selectedIndex, FALSE);
    }
    else
    {
        ClearPreviewCache();
        gSelectedErrorCount = 0;
        gSelectedCriticalCount = 0;
        SetWindowTextW(hLogContent, L"No log files found.");
    }

    gRefreshingLogFiles = false;

    if (!gLogFiles.empty())
    {
        ShowSelectedLog();
    }
}

std::optional<std::wstring> SelectDirectory(HWND owner)
{
    IFileOpenDialog* dialog = nullptr;

    HRESULT result = CoCreateInstance(
        CLSID_FileOpenDialog,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog)
    );

    if (FAILED(result))
    {
        MessageBoxW(
            owner,
            L"Could not create the folder selection dialog.",
            L"Error",
            MB_OK | MB_ICONERROR
        );

        return std::nullopt;
    }

    DWORD options = 0;

    result = dialog->GetOptions(&options);

    if (SUCCEEDED(result))
    {
        result = dialog->SetOptions(
            options |
            FOS_PICKFOLDERS |
            FOS_FORCEFILESYSTEM |
            FOS_PATHMUSTEXIST
        );
    }

    if (SUCCEEDED(result))
    {
        dialog->SetTitle(L"Select Cossacks 3 directory");
        result = dialog->Show(owner);
    }

    if (result == HRESULT_FROM_WIN32(ERROR_CANCELLED))
    {
        dialog->Release();
        return std::nullopt;
    }

    if (FAILED(result))
    {
        dialog->Release();

        MessageBoxW(
            owner,
            L"Could not select the folder.",
            L"Error",
            MB_OK | MB_ICONERROR
        );

        return std::nullopt;
    }

    IShellItem* selectedItem = nullptr;
    result = dialog->GetResult(&selectedItem);

    if (FAILED(result))
    {
        dialog->Release();
        return std::nullopt;
    }

    PWSTR selectedPath = nullptr;
    result = selectedItem->GetDisplayName(
        SIGDN_FILESYSPATH,
        &selectedPath
    );

    std::optional<std::wstring> directory;

    if (SUCCEEDED(result) && selectedPath != nullptr)
    {
        directory = selectedPath;
        CoTaskMemFree(selectedPath);
    }

    selectedItem->Release();
    dialog->Release();

    return directory;
}

void ShowSelectedLog()
{
    int selectedIndex = ListView_GetNextItem(hLogList, -1, LVNI_SELECTED);
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(gLogFiles.size())) return;

    const LogFileInfo& logFile = gLogFiles[selectedIndex];
    std::wstring rawContent = ReadTextFile(logFile.fullPath);
    LogSeverityCounts severity = CountLogSeverity(rawContent);
    gSelectedErrorCount = severity.errors;
    gSelectedCriticalCount = severity.critical;
    std::wstring content = FormatPreviewText(rawContent);

    const bool previewUnchanged =
        gPreviewCacheValid &&
        gPreviewFilePath == logFile.fullPath &&
        gPreviewModifiedTime == logFile.modifiedTime &&
        gPreviewContent == content;

    if (previewUnchanged)
    {
        UpdateStatusLabels();
        return;
    }

    gPreviewCacheValid = true;
    gPreviewFilePath = logFile.fullPath;
    gPreviewModifiedTime = logFile.modifiedTime;
    gPreviewContent = content;

    gUpdatingPreview = true;

    SetWindowTextW(hLogContent, content.c_str());
    HighlightCompileErrors();

    bool errorFound = SelectFirstCompileErrorLine();

    gUpdatingPreview = false;

    if (errorFound)
        UpdateErrorPane();
    else
        HideErrorPane();

    UpdateStatusLabels();
}

void ClearPreviewCache()
{
    gPreviewCacheValid = false;
    gPreviewFilePath.clear();
    gPreviewModifiedTime = {};
    gPreviewContent.clear();
}

void HighlightCompileErrors()
{
    const wchar_t* marker =
        L"CompileFramework() - compile global script error:";

    LONG searchFrom = 0;
    const LONG textLength =
        GetWindowTextLengthW(hLogContent);

    while (searchFrom < textLength)
    {
        FINDTEXTEXW search{};
        search.chrg.cpMin = searchFrom;
        search.chrg.cpMax = -1;
        search.lpstrText = const_cast<LPWSTR>(marker);

        LRESULT found = SendMessageW(hLogContent, EM_FINDTEXTEXW, FR_DOWN, reinterpret_cast<LPARAM>(&search));
        if (found == -1)
        {
            break;
        }

        // Find the Rich Edit line containing the matched text.
        LONG lineNumber = static_cast<LONG>(
            SendMessageW(
                hLogContent,
                EM_EXLINEFROMCHAR,
                0,
                search.chrgText.cpMin
            )
            );

        LONG lineStart = static_cast<LONG>(
            SendMessageW(
                hLogContent,
                EM_LINEINDEX,
                lineNumber,
                0
            )
            );

        LONG nextLineStart = static_cast<LONG>(
            SendMessageW(
                hLogContent,
                EM_LINEINDEX,
                lineNumber + 1,
                0
            )
            );

        LONG lineEnd = nextLineStart == -1
            ? textLength
            : nextLineStart;

        CHARRANGE lineRange{};
        lineRange.cpMin = lineStart;
        lineRange.cpMax = lineEnd;

        SendMessageW(
            hLogContent,
            EM_EXSETSEL,
            0,
            reinterpret_cast<LPARAM>(&lineRange)
        );

        CHARFORMAT2W format{};
        format.cbSize = sizeof(format);
        format.dwMask =
            CFM_COLOR |
            CFM_BACKCOLOR |
            CFM_BOLD;

        format.dwEffects = CFE_BOLD;
        format.crTextColor = RGB(153, 27, 27);
        format.crBackColor = RGB(254, 226, 226);

        SendMessageW(
            hLogContent,
            EM_SETCHARFORMAT,
            SCF_SELECTION,
            reinterpret_cast<LPARAM>(&format)
        );

        searchFrom = search.chrgText.cpMax;
    }

    HighlightErrorPrefixes();

    // Remove the visible text selection.
    CHARRANGE beginning{};
    beginning.cpMin = 0;
    beginning.cpMax = 0;

    SendMessageW(
        hLogContent,
        EM_EXSETSEL,
        0,
        reinterpret_cast<LPARAM>(&beginning)
    );
}

void HighlightErrorPrefixes()
{
    const LONG lineCount = static_cast<LONG>(
        SendMessageW(hLogContent, EM_GETLINECOUNT, 0, 0)
        );

    for (LONG line = 0; line < lineCount; line++)
    {
        const LONG lineStart = static_cast<LONG>(
            SendMessageW(hLogContent, EM_LINEINDEX, line, 0)
            );

        if (lineStart < 0)
        {
            continue;
        }

        wchar_t buffer[16]{};

        // First WORD must contain max number of characters to read.
        *reinterpret_cast<WORD*>(buffer) = 15;

        const LRESULT copied = SendMessageW(
            hLogContent,
            EM_GETLINE,
            line,
            reinterpret_cast<LPARAM>(buffer)
        );

        if (copied < 5)
        {
            continue;
        }

        std::wstring lineText(buffer, static_cast<size_t>(copied));

        if (!lineText.starts_with(L"ERROR"))
        {
            continue;
        }

        CHARRANGE range{};
        range.cpMin = lineStart;
        range.cpMax = lineStart + 5;

        SendMessageW(
            hLogContent,
            EM_EXSETSEL,
            0,
            reinterpret_cast<LPARAM>(&range)
        );

        CHARFORMAT2W format{};
        format.cbSize = sizeof(format);
        format.dwMask = CFM_COLOR | CFM_BOLD;
        format.dwEffects = CFE_BOLD;
        format.crTextColor = RGB(220, 38, 38);

        SendMessageW(
            hLogContent,
            EM_SETCHARFORMAT,
            SCF_SELECTION,
            reinterpret_cast<LPARAM>(&format)
        );
    }
}

bool IsCompileErrorLineAtCaret()
{
    std::wstring line = GetCaretLineText(hLogContent);

    return line.find(
        L"CompileFramework() - compile global script error:"
    ) != std::wstring::npos;
}

std::wstring GetCaretLineText(HWND edit)
{
    CHARRANGE selection{};

    SendMessageW(
        edit,
        EM_EXGETSEL,
        0,
        reinterpret_cast<LPARAM>(&selection)
    );

    LONG lineNumber = static_cast<LONG>(
        SendMessageW(
            edit,
            EM_EXLINEFROMCHAR,
            0,
            selection.cpMin
        )
        );

    LONG lineStart = static_cast<LONG>(
        SendMessageW(
            edit,
            EM_LINEINDEX,
            lineNumber,
            0
        )
        );

    if (lineStart < 0)
    {
        return {};
    }

    LONG lineLength = static_cast<LONG>(
        SendMessageW(
            edit,
            EM_LINELENGTH,
            lineStart,
            0
        )
        );

    if (lineLength <= 0)
    {
        return {};
    }

    std::wstring line(
        static_cast<size_t>(lineLength) + 1,
        L'\0'
    );

    *reinterpret_cast<WORD*>(line.data()) =
        static_cast<WORD>(
            std::min<LONG>(lineLength, 65534)
            );

    LRESULT copied = SendMessageW(
        edit,
        EM_GETLINE,
        lineNumber,
        reinterpret_cast<LPARAM>(line.data())
    );

    line.resize(static_cast<size_t>(copied));
    return line;
}

std::wstring GetIniPath()
{
    wchar_t exePath[MAX_PATH]{};

    GetModuleFileNameW(
        nullptr,
        exePath,
        ARRAYSIZE(exePath)
    );

    std::wstring path = exePath;

    const size_t slash = path.find_last_of(L"\\/");

    if (slash != std::wstring::npos)
    {
        path.resize(slash + 1);
    }
    else
    {
        path.clear();
    }

    return path + L"CossacksLogViewer.ini";
}

int ReadIniInt(
    const std::wstring& iniPath,
    const wchar_t* key,
    int defaultValue
)
{
    return GetPrivateProfileIntW(
        L"Window",
        key,
        defaultValue,
        iniPath.c_str()
    );
}

void WriteIniInt(
    const std::wstring& iniPath,
    const wchar_t* key,
    int value
)
{
    const std::wstring text = std::to_wstring(value);

    WritePrivateProfileStringW(
        L"Window",
        key,
        text.c_str(),
        iniPath.c_str()
    );
}

WINDOWPLACEMENT LoadWindowPlacement()
{
    constexpr int defaultWidth = 1600;
    constexpr int defaultHeight = 800;

    const std::wstring iniPath = GetIniPath();

    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);
    placement.flags = 0;

    const int saved = ReadIniInt(
        iniPath,
        L"Saved",
        0
    );

    if (!saved)
    {
        RECT workArea{};

        SystemParametersInfoW(
            SPI_GETWORKAREA,
            0,
            &workArea,
            0
        );

        const int x =
            workArea.left +
            ((workArea.right - workArea.left) - defaultWidth) / 2;

        const int y =
            workArea.top +
            ((workArea.bottom - workArea.top) - defaultHeight) / 2;

        placement.showCmd = SW_SHOWNORMAL;
        placement.rcNormalPosition = {
            x,
            y,
            x + defaultWidth,
            y + defaultHeight
        };

        NormalizeWindowPlacement(placement);
        return placement;
    }

    placement.showCmd =
        ReadIniInt(iniPath, L"Maximized", 0)
        ? SW_SHOWMAXIMIZED
        : SW_SHOWNORMAL;

    placement.rcNormalPosition.left =
        ReadIniInt(iniPath, L"Left", 100);

    placement.rcNormalPosition.top =
        ReadIniInt(iniPath, L"Top", 100);

    placement.rcNormalPosition.right =
        ReadIniInt(
            iniPath,
            L"Right",
            placement.rcNormalPosition.left + defaultWidth
        );

    placement.rcNormalPosition.bottom =
        ReadIniInt(
            iniPath,
            L"Bottom",
            placement.rcNormalPosition.top + defaultHeight
        );

    NormalizeWindowPlacement(placement);
    return placement;
}

void NormalizeWindowPlacement(WINDOWPLACEMENT& placement)
{
    RECT& rect = placement.rcNormalPosition;

    constexpr int minimumWidth = 900;
    constexpr int minimumHeight = 600;

    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    width = std::max(width, minimumWidth);
    height = std::max(height, minimumHeight);

    HMONITOR monitor = MonitorFromRect(
        &rect,
        MONITOR_DEFAULTTONEAREST
    );

    MONITORINFO monitorInfo{};
    monitorInfo.cbSize = sizeof(monitorInfo);

    if (!GetMonitorInfoW(monitor, &monitorInfo))
    {
        return;
    }

    const RECT& work = monitorInfo.rcWork;

    const int workWidth = work.right - work.left;
    const int workHeight = work.bottom - work.top;

    width = std::min(width, workWidth);
    height = std::min(height, workHeight);

    int left = rect.left;
    int top = rect.top;

    if (left < work.left)
    {
        left = work.left;
    }

    if (top < work.top)
    {
        top = work.top;
    }

    if (left + width > work.right)
    {
        left = work.right - width;
    }

    if (top + height > work.bottom)
    {
        top = work.bottom - height;
    }

    rect.left = left;
    rect.top = top;
    rect.right = left + width;
    rect.bottom = top + height;
}

void SaveWindowPlacement(HWND window)
{
    WINDOWPLACEMENT placement{};
    placement.length = sizeof(placement);

    if (!GetWindowPlacement(window, &placement))
    {
        return;
    }

    const std::wstring iniPath = GetIniPath();
    const RECT& rect = placement.rcNormalPosition;

    WriteIniInt(iniPath, L"Saved", 1);
    WriteIniInt(iniPath, L"Left", rect.left);
    WriteIniInt(iniPath, L"Top", rect.top);
    WriteIniInt(iniPath, L"Right", rect.right);
    WriteIniInt(iniPath, L"Bottom", rect.bottom);

    const bool maximized =
        placement.showCmd == SW_SHOWMAXIMIZED ||
        IsZoomed(window);

    WriteIniInt(
        iniPath,
        L"Maximized",
        maximized ? 1 : 0
    );
}



void ClearLoadedLogs()
{
    HideErrorPane();
    ClearPreviewCache();

    gLogFiles.clear();
    gSelectedErrorCount = 0;
    gSelectedCriticalCount = 0;

    if (hLogList)
    {
        ListView_DeleteAllItems(hLogList);
    }

    if (hLogContent)
    {
        SetWindowTextW(hLogContent, L"");
    }

    UpdateStatusLabels();
}

void SaveGameDirectory(const std::wstring& path)
{
    WritePrivateProfileStringW(
        L"Settings",
        L"GameDirectory",
        path.c_str(),
        GetIniPath().c_str()
    );
}

std::optional<std::wstring> LoadSavedGameDirectory()
{
    wchar_t buffer[4096]{};

    const DWORD length = GetPrivateProfileStringW(
        L"Settings",
        L"GameDirectory",
        L"",
        buffer,
        ARRAYSIZE(buffer),
        GetIniPath().c_str()
    );

    if (length == 0)
    {
        return std::nullopt;
    }

    std::wstring path = NormalizeGameDirectory(std::wstring(buffer, length));

    if (!IsValidGameDirectory(path))
    {
        return std::nullopt;
    }

    return path;
}

std::optional<std::wstring> ReadRegistryString(
    HKEY root,
    const wchar_t* subKey,
    const wchar_t* valueName,
    REGSAM extraFlags
)
{
    HKEY key = nullptr;

    LONG result = RegOpenKeyExW(
        root,
        subKey,
        0,
        KEY_READ | extraFlags,
        &key
    );

    if (result != ERROR_SUCCESS)
    {
        return std::nullopt;
    }

    DWORD type = 0;
    DWORD byteCount = 0;

    result = RegQueryValueExW(
        key,
        valueName,
        nullptr,
        &type,
        nullptr,
        &byteCount
    );

    if (result != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ))
    {
        RegCloseKey(key);
        return std::nullopt;
    }

    std::wstring value(
        byteCount / sizeof(wchar_t),
        L'\0'
    );

    result = RegQueryValueExW(
        key,
        valueName,
        nullptr,
        &type,
        reinterpret_cast<BYTE*>(value.data()),
        &byteCount
    );

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

        DWORD expandedLength = ExpandEnvironmentStringsW(
            value.c_str(),
            expanded,
            ARRAYSIZE(expanded)
        );

        if (expandedLength > 0 &&
            expandedLength <= ARRAYSIZE(expanded))
        {
            value = expanded;
        }
    }

    return value.empty()
        ? std::nullopt
        : std::optional<std::wstring>(value);
}

std::optional<std::wstring> DetectSteamGameDirectory()
{
    auto steamPath = ReadRegistryString(
        HKEY_CURRENT_USER,
        L"Software\\Valve\\Steam",
        L"SteamPath"
    );

    if (!steamPath)
    {
        return std::nullopt;
    }

    const std::wstring normalizedSteamPath = NormalizeGameDirectory(*steamPath);

    std::vector<fs::path> libraries;
    libraries.emplace_back(normalizedSteamPath);

    const fs::path libraryFile =
        fs::path(normalizedSteamPath) /
        L"steamapps" /
        L"libraryfolders.vdf";

    if (fs::is_regular_file(libraryFile))
    {
        std::wifstream file(libraryFile);

        if (file)
        {
            std::wstringstream buffer;
            buffer << file.rdbuf();

            const std::wstring text = buffer.str();

            const std::wregex pathPattern(
                LR"regex("path"\s+"([^"]+)")regex",
                std::regex_constants::icase
            );

            for (std::wsregex_iterator iterator(
                text.begin(),
                text.end(),
                pathPattern
            );
                iterator != std::wsregex_iterator();
                ++iterator)
            {
                std::wstring path = (*iterator)[1].str();

                // VDF may encode backslashes as \\.
                size_t position = 0;

                while ((position = path.find(
                    L"\\\\",
                    position
                )) != std::wstring::npos)
                {
                    path.replace(position, 2, L"\\");
                    position++;
                }

                libraries.emplace_back(NormalizeGameDirectory(path));
            }
        }
    }

    for (const fs::path& library : libraries)
    {
        auto gameDirectory =
            ReadSteamInstallDir(library);

        if (gameDirectory &&
            IsValidGameDirectory(*gameDirectory))
        {
            return NormalizeGameDirectory(*gameDirectory);
        }
    }

    return std::nullopt;
}

std::optional<std::wstring> ReadSteamInstallDir(
    const fs::path& library
)
{
    const fs::path manifest =
        library /
        L"steamapps" /
        L"appmanifest_333420.acf";

    if (!fs::is_regular_file(manifest))
    {
        return std::nullopt;
    }

    std::wifstream file(manifest);

    if (!file)
    {
        return std::nullopt;
    }

    std::wstringstream buffer;
    buffer << file.rdbuf();

    const std::wstring text = buffer.str();

    const std::wregex installPattern(
        LR"regex("installdir"\s+"([^"]+)")regex",
        std::regex_constants::icase
    );

    std::wsmatch match;

    if (!std::regex_search(
        text,
        match,
        installPattern))
    {
        return std::nullopt;
    }

    const fs::path result =
        library /
        L"steamapps" /
        L"common" /
        match[1].str();

    return NormalizeGameDirectory(result.wstring());
}

std::optional<std::wstring> DetectGogGameDirectory()
{
    constexpr wchar_t gogKey[] =
        L"SOFTWARE\\WOW6432Node\\GOG.com\\Games\\1797227701";

    const wchar_t* valueNames[] = {
        L"path",
        L"PATH",
        L"InstallLocation"
    };

    for (const wchar_t* valueName : valueNames)
    {
        auto path = ReadRegistryString(
            HKEY_LOCAL_MACHINE,
            gogKey,
            valueName,
            KEY_WOW64_32KEY
        );

        if (path)
        {
            std::wstring normalizedPath = NormalizeGameDirectory(*path);

            if (IsValidGameDirectory(normalizedPath))
            {
                return normalizedPath;
            }
        }
    }

    return std::nullopt;
}

std::optional<std::wstring> DetectGameDirectory()
{
    if (auto saved = LoadSavedGameDirectory())
    {
        return saved;
    }

    if (auto steam = DetectSteamGameDirectory())
    {
        return steam;
    }

    if (auto gog = DetectGogGameDirectory())
    {
        return gog;
    }

    return std::nullopt;
}



void LoadLogSortSetting()
{
    gSortDescending =
        GetPrivateProfileIntW(
            L"LogList",
            L"ModifiedDescending",
            1,
            GetIniPath().c_str()
        ) != 0;
}

void SaveLogSortSetting()
{
    WritePrivateProfileStringW(
        L"LogList",
        L"ModifiedDescending",
        gSortDescending ? L"1" : L"0",
        GetIniPath().c_str()
    );
}

void LoadAutoUpdateLogsSetting()
{
    gAutoUpdateLogs =
        GetPrivateProfileIntW(
            L"Settings",
            L"AutoUpdateLogs",
            1,
            GetIniPath().c_str()
        ) != 0;
}

void SaveAutoUpdateLogsSetting()
{
    WritePrivateProfileStringW(
        L"Settings",
        L"AutoUpdateLogs",
        gAutoUpdateLogs ? L"1" : L"0",
        GetIniPath().c_str()
    );
}

void UpdateAutoUpdateLogsMenu(HWND hWnd)
{
    HMENU menu = GetMenu(hWnd);
    if (!menu) return;

    CheckMenuItem(
        menu,
        IDM_AUTO_UPDATE_LOGS,
        MF_BYCOMMAND | (gAutoUpdateLogs ? MF_CHECKED : MF_UNCHECKED)
    );
}

bool SelectFirstCompileErrorLine()
{
    const wchar_t* marker = L"CompileFramework() - compile global script error:";

    FINDTEXTEXW search{};
    search.chrg.cpMin = 0;
    search.chrg.cpMax = -1;
    search.lpstrText = const_cast<LPWSTR>(marker);

    LRESULT found = SendMessageW(hLogContent, EM_FINDTEXTEXW, FR_DOWN, reinterpret_cast<LPARAM>(&search));
    if (found == -1) return false;

    CHARRANGE range{};
    range.cpMin = search.chrgText.cpMin;
    range.cpMax = search.chrgText.cpMin;

    SendMessageW(hLogContent, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
    SendMessageW(hLogContent, EM_SCROLLCARET, 0, 0);

    return true;
}

void UpdateStatusLabels()
{
    if (hListMetaLabel)
    {
        std::wstring text = gLogFiles.empty()
            ? L"No logs"
            : std::to_wstring(gLogFiles.size()) + L" file" + (gLogFiles.size() == 1 ? L"" : L"s");

        SetWindowTextW(hListMetaLabel, text.c_str());
    }

    int selectedIndex = hLogList
        ? ListView_GetNextItem(hLogList, -1, LVNI_SELECTED)
        : -1;

    std::wstring fileText = L"No log selected";
    std::wstring errorText = L"Errors: 0";
    std::wstring criticalText = L"Critical: 0";

    if (selectedIndex >= 0 && selectedIndex < static_cast<int>(gLogFiles.size()))
    {
        fileText = gLogFiles[selectedIndex].fileName;
        errorText = L"Errors: " + std::to_wstring(gSelectedErrorCount);
        criticalText = L"Critical: " + std::to_wstring(gSelectedCriticalCount);
    }

    if (hContentMetaLabel) SetWindowTextW(hContentMetaLabel, fileText.c_str());
    if (hErrorCountLabel) SetWindowTextW(hErrorCountLabel, errorText.c_str());
    if (hCriticalCountLabel)
    {
        SetWindowTextW(hCriticalCountLabel, criticalText.c_str());
        InvalidateRect(hCriticalCountLabel, nullptr, TRUE);
    }

    if (hErrorCountLabel) InvalidateRect(hErrorCountLabel, nullptr, TRUE);
}

void UpdateModifiedColumnTitle()
{
    wchar_t title[32]{};
    wcscpy_s(title, gSortDescending ? L"Modified \u25BC" : L"Modified \u25B2");

    LVCOLUMNW column{};
    column.mask = LVCF_TEXT;
    column.pszText = title;

    ListView_SetColumn(hLogList, 1, &column);
    InvalidateRect(hLogList, nullptr, TRUE);
    UpdateWindow(hLogList);
}

bool IsOnSplitter(HWND hWnd, int x, int y)
{
    RECT client{};
    GetClientRect(hWnd, &client);

    const int splitterLeft = 24 + gLeftPaneWidth + 8;
    const int splitterTop = 112;
    const int splitterBottom = client.bottom - 24;

    return x >= splitterLeft &&
        x <= splitterLeft + kSplitterWidth &&
        y >= splitterTop &&
        y <= splitterBottom;
}

void LoadRecentGameDirs()
{
    gRecentGameDirs.clear();

    const std::wstring iniPath = GetIniPath();

    for (int i = 0; i < 10; i++)
    {
        wchar_t key[32]{};
        swprintf_s(key, L"Dir%d", i);

        wchar_t buffer[4096]{};
        DWORD length = GetPrivateProfileStringW(L"RecentDirs", key, L"", buffer, ARRAYSIZE(buffer), iniPath.c_str());

        if (length == 0) continue;

        std::wstring path = NormalizeGameDirectory(std::wstring(buffer, length));
        if (!IsValidGameDirectory(path)) continue;

        gRecentGameDirs.push_back(path);
    }
}

void SaveRecentGameDirs()
{
    const std::wstring iniPath = GetIniPath();

    WritePrivateProfileStringW(L"RecentDirs", nullptr, nullptr, iniPath.c_str());

    for (size_t i = 0; i < gRecentGameDirs.size() && i < 10; i++)
    {
        wchar_t key[32]{};
        swprintf_s(key, L"Dir%zu", i);

        WritePrivateProfileStringW(L"RecentDirs", key, gRecentGameDirs[i].c_str(), iniPath.c_str());
    }
}

void AddRecentGameDir(const std::wstring& path)
{
    if (!IsValidGameDirectory(path)) return;

    std::wstring normalized = NormalizeGameDirectory(path);

    gRecentGameDirs.erase(
        std::remove(gRecentGameDirs.begin(), gRecentGameDirs.end(), normalized),
        gRecentGameDirs.end()
    );

    gRecentGameDirs.insert(gRecentGameDirs.begin(), normalized);

    if (gRecentGameDirs.size() > 10)
        gRecentGameDirs.resize(10);

    SaveRecentGameDirs();
}

void RefreshGameDirCombo()
{
    ComboBox_ResetContent(hBaseDir);

    for (const std::wstring& path : gRecentGameDirs)
        ComboBox_AddString(hBaseDir, path.c_str());
}

void CreateErrorPane(HWND owner)
{
    hErrorPane = CreateWindowExW(
        0,
        L"CossacksErrorPane",
        nullptr,
        WS_CHILD | WS_CLIPCHILDREN,
        0, 0, 100, kErrorPaneMaxHeight,
        owner,
        nullptr,
        hInst,
        nullptr
    );

    hErrorPaneContent = CreateWindowExW(
        0,
        MSFTEDIT_CLASS,
        L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_NOHIDESEL | WS_VSCROLL,
        4, 4, 92, kErrorPaneMaxHeight - 8,
        hErrorPane,
        nullptr,
        hInst,
        nullptr
    );

    SendMessageW(hErrorPaneContent, WM_SETFONT, reinterpret_cast<WPARAM>(gMonoFont), TRUE);
    SetWindowTheme(hErrorPaneContent, L"Explorer", nullptr);
    ApplyRichEditTheme(hErrorPaneContent, ColorWarningSurface, ColorText);
}

void UpdateErrorPane()
{
    if (!IsCompileErrorLineAtCaret())
    {
        HideErrorPane();
        return;
    }

    std::wstring text = core::ResolveScriptErrorText(
        NormalizeGameDirectory(GetControlText(hBaseDir)),
        GetCaretLineText(hLogContent)
    );

    SetWindowTextW(hErrorPaneContent, text.c_str());
    ShowErrorPane();
}

void ShowErrorPane()
{
    if (gErrorPaneVisible) return;

    gErrorPaneVisible = true;

    RECT client{};
    GetClientRect(GetParent(hLogContent), &client);
    LayoutControls(GetParent(hLogContent), client.right, client.bottom);
}

void HideErrorPane()
{
    if (!gErrorPaneVisible) return;

    gErrorPaneVisible = false;

    RECT client{};
    GetClientRect(GetParent(hLogContent), &client);
    LayoutControls(GetParent(hLogContent), client.right, client.bottom);
}

LRESULT CALLBACK ErrorPaneProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        RECT rect{};
        GetClientRect(hWnd, &rect);

        FillRect(hdc, &rect, gErrorPaneBrush);

        HBRUSH borderBrush = CreateSolidBrush(ColorAccent);
        FrameRect(hdc, &rect, borderBrush);
        DeleteObject(borderBrush);

        RECT accent{ rect.left, rect.top, rect.left + 4, rect.bottom };
        HBRUSH accentBrush = CreateSolidBrush(ColorAccent);
        FillRect(hdc, &accent, accentBrush);
        DeleteObject(accentBrush);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_SIZE:
        if (hErrorPaneContent) MoveWindow(hErrorPaneContent, 4, 4, LOWORD(lParam) - 8, HIWORD(lParam) - 8, TRUE);
        return 0;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

void LoadSplitterSetting()
{
    gLeftPaneWidth = GetPrivateProfileIntW(
        L"Layout",
        L"LeftPaneWidth",
        420,
        GetIniPath().c_str()
    );
}

void SaveSplitterSetting()
{
    WritePrivateProfileStringW(
        L"Layout",
        L"LeftPaneWidth",
        std::to_wstring(gLeftPaneWidth).c_str(),
        GetIniPath().c_str()
    );
}

void LoadLogColumnSettings()
{
    int fileWidth = GetPrivateProfileIntW(L"LogList", L"FileColumnWidth", 250, GetIniPath().c_str());
    int modifiedWidth = GetPrivateProfileIntW(L"LogList", L"ModifiedColumnWidth", 168, GetIniPath().c_str());

    fileWidth = std::clamp(fileWidth, 80, 1000);
    modifiedWidth = std::clamp(modifiedWidth, 100, 400);

    if (hLogList)
    {
        ListView_SetColumnWidth(hLogList, 0, fileWidth);
        ListView_SetColumnWidth(hLogList, 1, modifiedWidth);
    }
}

void SaveLogColumnSettings()
{
    if (!hLogList) return;

    int fileWidth = ListView_GetColumnWidth(hLogList, 0);
    int modifiedWidth = ListView_GetColumnWidth(hLogList, 1);

    WritePrivateProfileStringW(L"LogList", L"FileColumnWidth", std::to_wstring(fileWidth).c_str(), GetIniPath().c_str());
    WritePrivateProfileStringW(L"LogList", L"ModifiedColumnWidth", std::to_wstring(modifiedWidth).c_str(), GetIniPath().c_str());
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}

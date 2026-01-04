#include "system_tray.h"
#include "scan_runner.h"
#include <shellapi.h>
#include <sstream>
#include <iomanip>
#include <fstream>

// Define notification messages if not already defined (for older SDKs)
#ifndef NIN_SELECT
#define NIN_SELECT (WM_USER + 0)
#endif
#ifndef NIN_KEYSELECT
#define NIN_KEYSELECT (WM_USER + 1)
#endif
#ifndef NIN_BALLOONUSERCLICK
#define NIN_BALLOONUSERCLICK (WM_USER + 5)
#endif

// External references
extern HWND g_hMainWindow;

// Global instance
SystemTray* g_systemTray = nullptr;

SystemTray::SystemTray() 
    : m_hwnd(nullptr)
    , m_active(false)
    , m_scanTimerId(0)
    , m_tooltipTimerId(0)
    , m_pollingIntervalHours(2) {
    ZeroMemory(&m_nid, sizeof(m_nid));
    ZeroMemory(&m_nextScanTime, sizeof(m_nextScanTime));
}

SystemTray::~SystemTray() {
    RemoveFromTray();
}

bool SystemTray::Initialize(HWND hwnd) {
    m_hwnd = hwnd;
    
    m_nid.cbSize = sizeof(NOTIFYICONDATAW);
    m_nid.hWnd = hwnd;
    m_nid.uID = 1;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    
    // Load application icon (IDI_APP_ICON = 101)
    m_nid.hIcon = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(101), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    if (!m_nid.hIcon) {
        m_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }
    
    wcscpy_s(m_nid.szTip, L"WinUpdate");
    
    return true;
}

bool SystemTray::AddToTray() {
    if (m_active) return true;
    
    // Debug: Log tray icon setup
    std::ofstream log("C:\\Users\\NalleBerg\\AppData\\Roaming\\WinUpdate\\tray_debug.txt", std::ios::app);
    log << "AddToTray called\n";
    log << "  hwnd=" << m_nid.hWnd << "\n";
    log << "  uID=" << m_nid.uID << "\n";
    log << "  uFlags=0x" << std::hex << m_nid.uFlags << std::dec << "\n";
    log << "  uCallbackMessage=" << m_nid.uCallbackMessage << " (WM_TRAYICON=" << WM_TRAYICON << ")\n";
    log << "  hIcon=" << m_nid.hIcon << "\n";
    
    if (Shell_NotifyIconW(NIM_ADD, &m_nid)) {
        log << "NIM_ADD succeeded\n";
        m_active = true;
        
        // Set version for balloon support
        m_nid.uVersion = NOTIFYICON_VERSION_4;
        BOOL versionResult = Shell_NotifyIconW(NIM_SETVERSION, &m_nid);
        log << "NIM_SETVERSION result: " << versionResult << "\n";
        log.close();
        
        return true;
    }
    log << "NIM_ADD failed! Error: " << GetLastError() << "\n";
    log.close();
    return false;
}

bool SystemTray::RemoveFromTray() {
    if (!m_active) return true;
    
    StopScanTimer();
    StopTooltipTimer();
    
    if (Shell_NotifyIconW(NIM_DELETE, &m_nid)) {
        m_active = false;
        return true;
    }
    return false;
}

bool SystemTray::UpdateTooltip(const std::wstring& text) {
    if (!m_active) return false;
    
    wcscpy_s(m_nid.szTip, text.c_str());
    m_nid.uFlags = NIF_TIP;
    
    return Shell_NotifyIconW(NIM_MODIFY, &m_nid) != FALSE;
}

bool SystemTray::ShowBalloon(const std::wstring& title, const std::wstring& text) {
    if (!m_active) return false;
    
    m_nid.uFlags = NIF_INFO;
    wcscpy_s(m_nid.szInfoTitle, title.c_str());
    wcscpy_s(m_nid.szInfo, text.c_str());
    m_nid.dwInfoFlags = NIIF_INFO;
    m_nid.uTimeout = 10000; // 10 seconds
    
    bool result = Shell_NotifyIconW(NIM_MODIFY, &m_nid) != FALSE;
    
    // Reset flags
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    
    return result;
}

void SystemTray::ShowContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;
    
    // Add menu items with Unicode text
    AppendMenuW(hMenu, MF_STRING, IDM_SCAN_NOW, L"⚡ Scan now!");
    AppendMenuW(hMenu, MF_STRING, IDM_OPEN_WINDOW, L"🪟 Open main window");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"❌ Exit");
    
    // Required for menu to work properly
    SetForegroundWindow(hwnd);
    
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
    
    DestroyMenu(hMenu);
    
    // Required for proper menu closure
    PostMessage(hwnd, WM_NULL, 0, 0);
}

void SystemTray::StartScanTimer(int intervalHours) {
    m_pollingIntervalHours = intervalHours;
    
    // Convert hours to milliseconds
    UINT intervalMs = intervalHours * 60 * 60 * 1000;
    
    m_scanTimerId = SetTimer(m_hwnd, TIMER_SCAN, intervalMs, NULL);
    
    CalculateNextScanTime();
    UpdateNextScanTime();
}

void SystemTray::StopScanTimer() {
    if (m_scanTimerId) {
        KillTimer(m_hwnd, TIMER_SCAN);
        m_scanTimerId = 0;
    }
}

void SystemTray::StartTooltipTimer() {
    // Update tooltip every minute
    m_tooltipTimerId = SetTimer(m_hwnd, TIMER_TOOLTIP, 60000, NULL);
}

void SystemTray::StopTooltipTimer() {
    if (m_tooltipTimerId) {
        KillTimer(m_hwnd, TIMER_TOOLTIP);
        m_tooltipTimerId = 0;
    }
}

void SystemTray::CalculateNextScanTime() {
    SYSTEMTIME currentTime;
    GetLocalTime(&currentTime);
    
    // Convert to FILETIME for calculation
    FILETIME ftCurrent, ftNext;
    SystemTimeToFileTime(&currentTime, &ftCurrent);
    
    // Convert to ULARGE_INTEGER for addition
    ULARGE_INTEGER uliNext;
    uliNext.LowPart = ftCurrent.dwLowDateTime;
    uliNext.HighPart = ftCurrent.dwHighDateTime;
    
    // Add interval (in 100-nanosecond intervals)
    // 1 hour = 36000000000 hundred-nanoseconds
    ULONGLONG intervalInHundredNanos = (ULONGLONG)m_pollingIntervalHours * 36000000000ULL;
    uliNext.QuadPart += intervalInHundredNanos;
    
    // Convert back
    ftNext.dwLowDateTime = uliNext.LowPart;
    ftNext.dwHighDateTime = uliNext.HighPart;
    
    FileTimeToSystemTime(&ftNext, &m_nextScanTime);
}

void SystemTray::UpdateNextScanTime() {
    std::wstring timeStr = GetNextScanTimeString();
    std::wstring tooltip = L"WinUpdate - Next scan: " + timeStr;
    UpdateTooltip(tooltip);
}

std::wstring SystemTray::GetNextScanTimeString() {
    // Get current time
    SYSTEMTIME currentTime;
    GetLocalTime(&currentTime);
    
    // Convert both times to FILETIME for comparison
    FILETIME ftCurrent, ftNext;
    SystemTimeToFileTime(&currentTime, &ftCurrent);
    SystemTimeToFileTime(&m_nextScanTime, &ftNext);
    
    // Convert to ULARGE_INTEGER for subtraction
    ULARGE_INTEGER uliCurrent, uliNext;
    uliCurrent.LowPart = ftCurrent.dwLowDateTime;
    uliCurrent.HighPart = ftCurrent.dwHighDateTime;
    uliNext.LowPart = ftNext.dwLowDateTime;
    uliNext.HighPart = ftNext.dwHighDateTime;
    
    // Calculate difference in 100-nanosecond intervals
    LONGLONG diff = uliNext.QuadPart - uliCurrent.QuadPart;
    
    // Convert to minutes and hours
    LONGLONG totalMinutes = diff / 600000000LL; // 100-nanoseconds to minutes
    int hours = (int)(totalMinutes / 60);
    int minutes = (int)(totalMinutes % 60);
    
    // Format as HH:MM
    std::wstringstream ss;
    ss << std::setfill(L'0') << std::setw(2) << hours 
       << L":" 
       << std::setfill(L'0') << std::setw(2) << minutes;
    return ss.str();
}

void SystemTray::TriggerScan() {
    // Reset scan timer
    StopScanTimer();
    StartScanTimer(m_pollingIntervalHours);
    
    // Trigger scan by posting refresh message to main window
    // The main window will handle the scan and check skip configuration
    extern HWND g_hMainWindow;
    if (g_hMainWindow) {
        // WM_REFRESH_ASYNC is defined in main.cpp as (WM_APP + 1)
        PostMessageW(g_hMainWindow, WM_APP + 1, 1, 0);
    }
}

LRESULT SystemTray::HandleTrayMessage(HWND hwnd, WPARAM wParam, LPARAM lParam) {
    // With NOTIFYICON_VERSION_4, the icon ID is in HIWORD(lParam), not wParam
    UINT iconId = HIWORD(lParam);
    UINT uMsg = LOWORD(lParam);
    
    // Debug logging - write to file what message we received
    {
        std::ofstream log("C:\\Users\\NalleBerg\\AppData\\Roaming\\WinUpdate\\tray_debug.txt", std::ios::app);
        log << "Tray message: wParam=" << wParam << ", lParam=0x" << std::hex << lParam 
            << ", LOWORD=" << LOWORD(lParam) << ", HIWORD=" << HIWORD(lParam) << std::dec 
            << " (iconId=" << iconId << ", uMsg=" << uMsg << ")\\n";
        log.close();
    }
    
    // Check if this is for our icon
    if (iconId != 1) return 0;
    
    switch (uMsg) {
        case NIN_SELECT:          // Left click (version 4)
        case NIN_KEYSELECT:       // Keyboard selection (version 4)
        case WM_LBUTTONUP:        // Left click (fallback for older versions)
            // Show main window
            ShowWindow(hwnd, SW_SHOW);
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            break;
            
        case WM_CONTEXTMENU:      // Right-click (version 4)
        case WM_RBUTTONUP:        // Right-click (fallback for older versions)
            // Show context menu
            if (g_systemTray) {
                g_systemTray->ShowContextMenu(hwnd);
            }
            break;
            
        case NIN_BALLOONUSERCLICK:  // Balloon notification clicked
            // Show main window
            ShowWindow(hwnd, SW_SHOW);
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
            break;
    }
    
    return 0;
}

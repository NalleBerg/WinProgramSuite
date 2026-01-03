#include "Config.h"
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <fstream>
#include <sstream>

// Dialog control IDs
#define IDC_CHK_SYSTRAY 3001
#define IDC_COMBO_POLLING 3002
#define IDC_BTN_OK 3003
#define IDC_BTN_CANCEL 3004

// Polling interval options (prime numbers in hours)
static const int POLLING_INTERVALS[] = {0, 2, 3, 5, 7, 11, 13, 17, 19, 23};
static const int POLLING_COUNT = 10;

// Settings structure
struct ConfigSettings {
    bool enableSysTray = false;
    int pollingInterval = 0;  // 0 = scan only at startup
};

static std::wstring Utf8ToWide(const std::string &s) {
    if (s.empty()) return {};
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), NULL, 0);
    if (wideLen <= 0) return std::wstring(s.begin(), s.end());
    std::wstring out(wideLen, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &out[0], wideLen);
    return out;
}

static std::string WideToUtf8(const std::wstring &w) {
    if (w.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), NULL, 0, NULL, NULL);
    if (size <= 0) return std::string();
    std::string out(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &out[0], size, NULL, NULL);
    return out;
}

static std::string GetSettingsPath() {
    char buf[MAX_PATH];
    DWORD len = GetEnvironmentVariableA("APPDATA", buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return std::string(buf) + "\\WinUpdate\\wup_settings.ini";
    }
    return "wup_settings.ini";
}

static ConfigSettings LoadSettings() {
    ConfigSettings settings;
    std::string path = GetSettingsPath();
    std::ifstream ifs(path);
    if (!ifs) return settings;
    
    std::string line;
    bool inSysTray = false;
    while (std::getline(ifs, line)) {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);
        
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        
        if (line == "[systray]") {
            inSysTray = true;
            continue;
        } else if (line[0] == '[') {
            inSysTray = false;
            continue;
        }
        
        if (inSysTray) {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = line.substr(0, eq);
                std::string val = line.substr(eq + 1);
                // Trim key and value
                size_t ks = key.find_first_not_of(" \t");
                size_t ke = key.find_last_not_of(" \t");
                if (ks != std::string::npos) key = key.substr(ks, ke - ks + 1);
                size_t vs = val.find_first_not_of(" \t");
                size_t ve = val.find_last_not_of(" \t");
                if (vs != std::string::npos) val = val.substr(vs, ve - vs + 1);
                
                if (key == "enabled") {
                    settings.enableSysTray = (val == "1" || val == "true");
                } else if (key == "polling_interval") {
                    settings.pollingInterval = std::stoi(val);
                }
            }
        }
    }
    return settings;
}

static void SaveSettings(const ConfigSettings &settings) {
    std::string path = GetSettingsPath();
    
    // Read existing file to preserve other sections
    std::stringstream content;
    std::ifstream ifs(path);
    std::string line;
    bool inSysTray = false;
    bool sysTrayWritten = false;
    
    if (ifs) {
        while (std::getline(ifs, line)) {
            if (line.find("[systray]") != std::string::npos) {
                inSysTray = true;
                sysTrayWritten = true;
                content << "[systray]\n";
                content << "enabled=" << (settings.enableSysTray ? "1" : "0") << "\n";
                content << "polling_interval=" << settings.pollingInterval << "\n";
                continue;
            } else if (!line.empty() && line[0] == '[') {
                inSysTray = false;
            }
            
            if (!inSysTray) {
                content << line << "\n";
            }
        }
        ifs.close();
    }
    
    // If [systray] section didn't exist, add it
    if (!sysTrayWritten) {
        content << "\n[systray]\n";
        content << "enabled=" << (settings.enableSysTray ? "1" : "0") << "\n";
        content << "polling_interval=" << settings.pollingInterval << "\n";
    }
    
    // Write back
    std::ofstream ofs(path);
    if (ofs) {
        ofs << content.str();
    }
}

static INT_PTR CALLBACK ConfigDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static ConfigSettings* pSettings = nullptr;
    
    switch (message) {
    case WM_INITDIALOG: {
        pSettings = (ConfigSettings*)lParam;
        
        // Set checkbox state
        CheckDlgButton(hDlg, IDC_CHK_SYSTRAY, pSettings->enableSysTray ? BST_CHECKED : BST_UNCHECKED);
        
        // Populate combo box
        HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_POLLING);
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Scan only at Windows startup");
        for (int i = 1; i < POLLING_COUNT; i++) {
            std::wstring text = L"Scan at Windows startup and every " + 
                                std::to_wstring(POLLING_INTERVALS[i]) + L" hours";
            SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)text.c_str());
        }
        
        // Select current interval
        int selIdx = 0;
        for (int i = 0; i < POLLING_COUNT; i++) {
            if (POLLING_INTERVALS[i] == pSettings->pollingInterval) {
                selIdx = i;
                break;
            }
        }
        SendMessageW(hCombo, CB_SETCURSEL, selIdx, 0);
        
        // Enable/disable combo based on checkbox
        EnableWindow(hCombo, pSettings->enableSysTray);
        
        return TRUE;
    }
    
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        
        if (id == IDC_CHK_SYSTRAY && code == BN_CLICKED) {
            BOOL checked = IsDlgButtonChecked(hDlg, IDC_CHK_SYSTRAY) == BST_CHECKED;
            EnableWindow(GetDlgItem(hDlg, IDC_COMBO_POLLING), checked);
        } else if (id == IDC_BTN_OK) {
            // Save settings
            pSettings->enableSysTray = IsDlgButtonChecked(hDlg, IDC_CHK_SYSTRAY) == BST_CHECKED;
            
            HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_POLLING);
            int selIdx = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
            if (selIdx >= 0 && selIdx < POLLING_COUNT) {
                pSettings->pollingInterval = POLLING_INTERVALS[selIdx];
            }
            
            EndDialog(hDlg, IDOK);
            return TRUE;
        } else if (id == IDC_BTN_CANCEL || id == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    
    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    
    return FALSE;
}

bool ShowConfigDialog(HWND parent) {
    // Load current settings
    ConfigSettings settings = LoadSettings();
    ConfigSettings oldSettings = settings;
    
    // Center dialog on parent
    RECT rcParent;
    GetWindowRect(parent, &rcParent);
    int parentCenterX = (rcParent.left + rcParent.right) / 2;
    int parentCenterY = (rcParent.top + rcParent.bottom) / 2;
    
    // Create dialog template in memory
    const int dlgW = 400, dlgH = 180;
    int dlgX = parentCenterX - dlgW / 2;
    int dlgY = parentCenterY - dlgH / 2;
    
    // Use simple CreateWindow approach for quick implementation
    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"#32770",  // Dialog class
        L"Configuration",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        dlgX, dlgY, dlgW, dlgH,
        parent,
        NULL,
        GetModuleHandleW(NULL),
        NULL
    );
    
    if (!hDlg) return false;
    
    // Create controls
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    
    // Checkbox
    HWND hCheck = CreateWindowExW(0, L"Button", 
        L"Open at Windows startup in system tray",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        20, 20, 360, 24,
        hDlg, (HMENU)IDC_CHK_SYSTRAY, NULL, NULL);
    SendMessageW(hCheck, WM_SETFONT, (WPARAM)hFont, TRUE);
    CheckDlgButton(hDlg, IDC_CHK_SYSTRAY, settings.enableSysTray ? BST_CHECKED : BST_UNCHECKED);
    
    // Combo box
    HWND hCombo = CreateWindowExW(0, L"ComboBox", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        20, 55, 360, 200,
        hDlg, (HMENU)IDC_COMBO_POLLING, NULL, NULL);
    SendMessageW(hCombo, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // Populate combo
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Scan only at Windows startup");
    for (int i = 1; i < POLLING_COUNT; i++) {
        std::wstring text = L"Scan at Windows startup and every " + 
                            std::to_wstring(POLLING_INTERVALS[i]) + L" hours";
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)text.c_str());
    }
    
    // Select current interval
    int selIdx = 0;
    for (int i = 0; i < POLLING_COUNT; i++) {
        if (POLLING_INTERVALS[i] == settings.pollingInterval) {
            selIdx = i;
            break;
        }
    }
    SendMessageW(hCombo, CB_SETCURSEL, selIdx, 0);
    EnableWindow(hCombo, settings.enableSysTray);
    
    // OK button
    HWND hOK = CreateWindowExW(0, L"Button", L"OK",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
        dlgW - 180, dlgH - 50, 75, 28,
        hDlg, (HMENU)IDC_BTN_OK, NULL, NULL);
    SendMessageW(hOK, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // Cancel button
    HWND hCancel = CreateWindowExW(0, L"Button", L"Cancel",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        dlgW - 95, dlgH - 50, 75, 28,
        hDlg, (HMENU)IDC_BTN_CANCEL, NULL, NULL);
    SendMessageW(hCancel, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // Message loop
    MSG msg;
    bool dialogResult = false;
    bool dialogDone = false;
    
    SetFocus(hCheck);
    
    while (!dialogDone && GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.hwnd == hDlg || IsChild(hDlg, msg.hwnd)) {
            // Handle dialog messages
            if (msg.message == WM_COMMAND) {
                int id = LOWORD(msg.wParam);
                int code = HIWORD(msg.wParam);
                
                if (id == IDC_CHK_SYSTRAY && code == BN_CLICKED) {
                    BOOL checked = IsDlgButtonChecked(hDlg, IDC_CHK_SYSTRAY) == BST_CHECKED;
                    EnableWindow(hCombo, checked);
                } else if (id == IDC_BTN_OK) {
                    // Save settings
                    settings.enableSysTray = IsDlgButtonChecked(hDlg, IDC_CHK_SYSTRAY) == BST_CHECKED;
                    
                    int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
                    if (sel >= 0 && sel < POLLING_COUNT) {
                        settings.pollingInterval = POLLING_INTERVALS[sel];
                    }
                    
                    SaveSettings(settings);
                    dialogResult = true;
                    dialogDone = true;
                } else if (id == IDC_BTN_CANCEL || id == IDCANCEL) {
                    dialogDone = true;
                }
            } else if (msg.message == WM_CLOSE) {
                dialogDone = true;
            } else if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
                dialogDone = true;
            }
            
            if (!IsDialogMessageW(hDlg, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } else {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    
    DestroyWindow(hDlg);
    
    // Return true if settings changed
    return dialogResult && (settings.enableSysTray != oldSettings.enableSysTray || 
                            settings.pollingInterval != oldSettings.pollingInterval);
}

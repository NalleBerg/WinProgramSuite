#include "Config.h"
#include <windows.h>
#include <commctrl.h>
#include <string>
#include <fstream>
#include <sstream>
#include <unordered_map>

// Dialog control IDs
#define IDC_CHK_SYSTRAY 3001
#define IDC_COMBO_POLLING 3002
#define IDC_LBL_STATUS 3003
#define IDC_BTN_APPLY 3004
#define IDC_BTN_OK 3005
#define IDC_BTN_CANCEL 3006

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

// Load i18n translations
static std::unordered_map<std::string, std::wstring> LoadTranslations() {
    std::unordered_map<std::string, std::wstring> trans;
    
    // Load current locale from settings
    std::string locale = "en_GB";
    std::string settingsPath = GetSettingsPath();
    std::ifstream ifs(settingsPath);
    if (ifs) {
        std::string line;
        bool inLang = false;
        while (std::getline(ifs, line)) {
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            line = line.substr(start);
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            if (line == "[language]") {
                inLang = true;
                continue;
            } else if (line[0] == '[') {
                inLang = false;
                continue;
            }
            if (inLang && !line.empty()) {
                locale = line;
                break;
            }
        }
        ifs.close();
    }
    
    // Load translations from i18n file in executable directory
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring exeDir = exePath;
    size_t lastSlash = exeDir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        exeDir = exeDir.substr(0, lastSlash);
    }
    std::wstring i18nPathW = exeDir + L"\\i18n\\" + Utf8ToWide(locale) + L".txt";
    
    std::ifstream i18nFile(i18nPathW.c_str());
    if (i18nFile) {
        std::string line;
        while (std::getline(i18nFile, line)) {
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            line = line.substr(start);
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                std::string key = line.substr(0, eq);
                std::string val = line.substr(eq + 1);
                trans[key] = Utf8ToWide(val);
            }
        }
    }
    
    return trans;
}

static std::wstring t(const std::unordered_map<std::string, std::wstring> &trans, const std::string &key) {
    auto it = trans.find(key);
    if (it != trans.end()) return it->second;
    return Utf8ToWide(key);
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
        
        if (line == "[systemtraystatus]") {
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
            if (line.find("[systemtraystatus]") != std::string::npos) {
                inSysTray = true;
                sysTrayWritten = true;
                content << "[systemtraystatus]\n";
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
    
    // If [systemtraystatus] section didn't exist, add it
    if (!sysTrayWritten) {
        content << "\n[systemtraystatus]\n";
        content << "enabled=" << (settings.enableSysTray ? "1" : "0") << "\n";
        content << "polling_interval=" << settings.pollingInterval << "\n";
    }
    
    // Write back
    std::ofstream ofs(path);
    if (ofs) {
        ofs << content.str();
    }
}

static void UpdateStatusLabel(HWND hDlg, HWND hStatus, const ConfigSettings &settings, const std::unordered_map<std::string, std::wstring> &trans) {
    std::wstring status;
    if (!settings.enableSysTray) {
        status = t(trans, "config_status_disabled");
    } else if (settings.pollingInterval == 0) {
        status = t(trans, "config_status_startup");
    } else {
        wchar_t buf[256];
        swprintf(buf, 256, t(trans, "config_status_polling").c_str(), settings.pollingInterval);
        status = buf;
    }
    SetWindowTextW(hStatus, status.c_str());
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
    // Load translations
    auto trans = LoadTranslations();
    
    // Load current settings
    ConfigSettings settings = LoadSettings();
    ConfigSettings originalSettings = settings;
    
    // Ensure .ini file has [systemtraystatus] section - create if missing
    std::string settingsPath = GetSettingsPath();
    std::ifstream testIfs(settingsPath);
    bool hasSysTraySection = false;
    if (testIfs) {
        std::string line;
        while (std::getline(testIfs, line)) {
            if (line.find("[systemtraystatus]") != std::string::npos) {
                hasSysTraySection = true;
                break;
            }
        }
        testIfs.close();
    }
    if (!hasSysTraySection) {
        // Create the section with default values
        SaveSettings(settings);
    }
    
    // Center dialog on parent
    RECT rcParent;
    GetWindowRect(parent, &rcParent);
    int parentCenterX = (rcParent.left + rcParent.right) / 2;
    int parentCenterY = (rcParent.top + rcParent.bottom) / 2;
    
    // Create dialog
    const int dlgW = 460, dlgH = 270;
    int dlgX = parentCenterX - dlgW / 2;
    int dlgY = parentCenterY - dlgH / 2;
    
    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        L"#32770",  // Dialog class
        t(trans, "config_dialog_title").c_str(),
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
        t(trans, "config_systray_chk").c_str(),
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
        20, 20, 420, 24,
        hDlg, (HMENU)IDC_CHK_SYSTRAY, NULL, NULL);
    SendMessageW(hCheck, WM_SETFONT, (WPARAM)hFont, TRUE);
    CheckDlgButton(hDlg, IDC_CHK_SYSTRAY, settings.enableSysTray ? BST_CHECKED : BST_UNCHECKED);
    
    // Combo box
    HWND hCombo = CreateWindowExW(0, L"ComboBox", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP,
        20, 55, 420, 200,
        hDlg, (HMENU)IDC_COMBO_POLLING, NULL, NULL);
    SendMessageW(hCombo, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // Populate combo
    SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)t(trans, "config_polling_startup").c_str());
    for (int i = 1; i < POLLING_COUNT; i++) {
        wchar_t buf[256];
        swprintf(buf, 256, t(trans, "config_polling_hours").c_str(), POLLING_INTERVALS[i]);
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)buf);
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
    
    // Enable/disable combo based on checkbox state - must be after CheckDlgButton
    BOOL shouldEnable = settings.enableSysTray ? TRUE : FALSE;
    EnableWindow(hCombo, shouldEnable);
    
    // Status label (centered between dropdown and buttons)
    HWND hStatus = CreateWindowExW(0, L"Static", L"",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        20, 95, 420, 20,
        hDlg, (HMENU)IDC_LBL_STATUS, NULL, NULL);
    SendMessageW(hStatus, WM_SETFONT, (WPARAM)hFont, TRUE);
    UpdateStatusLabel(hDlg, hStatus, settings, trans);
    
    // Apply button
    HWND hApply = CreateWindowExW(0, L"Button", t(trans, "config_btn_use").c_str(),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        dlgW - 310, dlgH - 70, 90, 32,
        hDlg, (HMENU)IDC_BTN_APPLY, NULL, NULL);
    SendMessageW(hApply, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // OK button
    HWND hOK = CreateWindowExW(0, L"Button", t(trans, "config_btn_ok").c_str(),
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
        dlgW - 210, dlgH - 70, 90, 32,
        hDlg, (HMENU)IDC_BTN_OK, NULL, NULL);
    SendMessageW(hOK, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // Cancel button
    HWND hCancel = CreateWindowExW(0, L"Button", t(trans, "config_btn_cancel").c_str(),
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        dlgW - 110, dlgH - 70, 90, 32,
        hDlg, (HMENU)IDC_BTN_CANCEL, NULL, NULL);
    SendMessageW(hCancel, WM_SETFONT, (WPARAM)hFont, TRUE);
    
    // Subclass dialog to handle messages
    struct DialogData {
        ConfigSettings* settings;
        HWND hCombo;
        HWND hStatus;
        const std::unordered_map<std::string, std::wstring>* trans;
        bool* dialogResult;
        bool* dialogDone;
    };
    
    DialogData dlgData;
    dlgData.settings = &settings;
    dlgData.hCombo = hCombo;
    dlgData.hStatus = hStatus;
    dlgData.trans = &trans;
    bool dialogResult = false;
    bool dialogDone = false;
    dlgData.dialogResult = &dialogResult;
    dlgData.dialogDone = &dialogDone;
    
    auto DlgProc = [](HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) -> LRESULT {
        DialogData* pData = (DialogData*)data;
        
        if (msg == WM_COMMAND) {
            int id = LOWORD(wp);
            int code = HIWORD(wp);
            
            if (id == IDC_CHK_SYSTRAY && code == BN_CLICKED) {
                BOOL checked = (IsDlgButtonChecked(hwnd, IDC_CHK_SYSTRAY) == BST_CHECKED);
                EnableWindow(pData->hCombo, checked);
                pData->settings->enableSysTray = (checked == TRUE);
                return 0;
            } else if (id == IDC_COMBO_POLLING && code == CBN_SELCHANGE) {
                int sel = (int)SendMessageW(pData->hCombo, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < POLLING_COUNT) {
                    pData->settings->pollingInterval = POLLING_INTERVALS[sel];
                }
                return 0;
            } else if (id == IDC_BTN_APPLY) {
                pData->settings->enableSysTray = IsDlgButtonChecked(hwnd, IDC_CHK_SYSTRAY) == BST_CHECKED;
                int sel = (int)SendMessageW(pData->hCombo, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < POLLING_COUNT) {
                    pData->settings->pollingInterval = POLLING_INTERVALS[sel];
                }
                SaveSettings(*pData->settings);
                // Reload settings from .ini to ensure status reflects saved state
                ConfigSettings savedSettings = LoadSettings();
                UpdateStatusLabel(hwnd, pData->hStatus, savedSettings, *pData->trans);
                *pData->dialogResult = true;
                return 0;
            } else if (id == IDC_BTN_OK) {
                pData->settings->enableSysTray = IsDlgButtonChecked(hwnd, IDC_CHK_SYSTRAY) == BST_CHECKED;
                int sel = (int)SendMessageW(pData->hCombo, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < POLLING_COUNT) {
                    pData->settings->pollingInterval = POLLING_INTERVALS[sel];
                }
                SaveSettings(*pData->settings);
                *pData->dialogResult = true;
                *pData->dialogDone = true;
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            } else if (id == IDC_BTN_CANCEL || id == IDCANCEL) {
                *pData->dialogResult = false;
                *pData->dialogDone = true;
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                return 0;
            }
        } else if (msg == WM_CLOSE) {
            *pData->dialogDone = true;
            return 0;
        }
        
        return DefSubclassProc(hwnd, msg, wp, lp);
    };
    
    SetWindowSubclass(hDlg, DlgProc, 0, (DWORD_PTR)&dlgData);
    
    SetFocus(hCheck);
    
    // Message loop
    MSG msg;
    while (!dialogDone && GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    
    RemoveWindowSubclass(hDlg, DlgProc, 0);
    DestroyWindow(hDlg);
    
    // Return true if settings changed
    return dialogResult && (settings.enableSysTray != originalSettings.enableSysTray || 
                            settings.pollingInterval != originalSettings.pollingInterval);
}

#include "install_dialog.h"
#include <commctrl.h>
#include <shellapi.h>
#include <thread>
#include <regex>
#include <sstream>
#include <fstream>

#pragma comment(lib, "comctl32.lib")

// Animation state
static int g_animFrame = 0;
static HWND g_hInstallAnim = NULL;
static std::wstring g_doneButtonText = L"Done!";

// Animation subclass procedure (draws moving dot overlay on progress bar)
static LRESULT CALLBACK AnimSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR) {
    if (uMsg == WM_TIMER && wParam == 0xBEEF) {
        g_animFrame++;
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }
    if (uMsg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; 
        GetClientRect(hwnd, &rc);
        
        // Clear entire area (erase previous dot)
        FillRect(hdc, &rc, (HBRUSH)(COLOR_BTNFACE + 1));
        
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;
        
        // Size of the dot (almost full height)
        int dotSize = height - 6;  // 3px margin on each side
        
        // Movement range (dot travels from left edge to right edge)
        int travelDistance = width - dotSize;
        
        // Position cycles: move 1 pixel per frame
        int position = (g_animFrame * 1) % (travelDistance + dotSize);
        
        // When dot exits right side, reset
        if (position > travelDistance) {
            position = 0;
        }
        
        // Draw the round dot
        HBRUSH hBrush = CreateSolidBrush(RGB(0, 120, 215));
        HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(0, 120, 215));
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
        
        int centerX = position + dotSize / 2;
        int centerY = height / 2;
        int radius = dotSize / 2;
        
        Ellipse(hdc, centerX - radius, centerY - radius, centerX + radius, centerY + radius);
        
        SelectObject(hdc, hOldBrush);
        SelectObject(hdc, hOldPen);
        DeleteObject(hBrush);
        DeleteObject(hPen);
        
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (uMsg == WM_ERASEBKGND) {
        return 1;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

// UTF-8 to Wide string conversion
static std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
    if (needed <= 0) return L"";
    std::wstring wide(needed, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &wide[0], needed);
    return wide;
}

// Dialog window procedure
static LRESULT CALLBACK InstallDlgProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static HWND hProg = NULL;
    static HWND hOut = NULL;
    static HWND hStatus = NULL;
    static HWND hAnim = NULL;
    static HWND hDone = NULL;
    static HWND hOverallStatus = NULL;
    
    switch (uMsg) {
    case WM_CREATE: {
        HINSTANCE hInst = GetModuleHandleW(NULL);
        INITCOMMONCONTROLSEX icce{ sizeof(icce), ICC_PROGRESS_CLASS };
        InitCommonControlsEx(&icce);
        
        const int W = 640, H = 480;
        
        // Overall progress label (centered)
        hOverallStatus = CreateWindowExW(0, L"Static", L"Installing 0 of 0", WS_CHILD | WS_VISIBLE | SS_CENTER, 
            20, 20, W-40, 24, hwnd, (HMENU)1002, hInst, NULL);
        HFONT hFontBold = CreateFontW(-14, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        SendMessageW(hOverallStatus, WM_SETFONT, (WPARAM)hFontBold, TRUE);
        
        // Status label (package-specific, above progress bar)
        HFONT hFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
        hStatus = CreateWindowExW(0, L"Static", L"Preparing...", WS_CHILD | WS_VISIBLE | SS_LEFT, 
            24, 48, W-48, 20, hwnd, NULL, hInst, NULL);
        SendMessageW(hStatus, WM_SETFONT, (WPARAM)hFont, TRUE);
        
        // Progress bar container (groove)
        HWND hGroove = CreateWindowExW(WS_EX_CLIENTEDGE, L"Static", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 
            20, 72, W-40, 28, hwnd, NULL, hInst, NULL);
        
        // Progress bar
        hProg = CreateWindowExW(0, PROGRESS_CLASSW, NULL, WS_CHILD | WS_VISIBLE, 
            24, 76, W-48, 20, hwnd, NULL, hInst, NULL);
        SendMessageW(hProg, PBM_SETRANGE, 0, MAKELPARAM(0, 3));
        SendMessageW(hProg, PBM_SETPOS, 0, 0);
        
        // Animation overlay (hidden initially, shown during install phase)
        hAnim = CreateWindowExW(WS_EX_TRANSPARENT, L"STATIC", NULL, WS_CHILD, 
            20, 72, W-40, 28, hwnd, NULL, hInst, NULL);
        g_hInstallAnim = hAnim;
        SetWindowSubclass(hAnim, AnimSubclassProc, 0, 0);
        SetTimer(hAnim, 0xBEEF, 1, NULL); // 1ms refresh rate
        
        // Output edit (larger area)
        hOut = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", NULL, 
            WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL,
            20, 110, W-40, H-190, hwnd, NULL, hInst, NULL);
        HFONT hEditFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            FIXED_PITCH | FF_MODERN, L"Consolas");
        SendMessageW(hOut, WM_SETFONT, (WPARAM)hEditFont, TRUE);
        
        // Done button (centered, disabled initially)
        hDone = CreateWindowExW(0, L"Button", g_doneButtonText.c_str(), WS_CHILD | WS_VISIBLE | WS_DISABLED | BS_PUSHBUTTON, 
            (W-150)/2, H-70, 150, 32, hwnd, (HMENU)1001, hInst, NULL);
        
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 1001) {
            // Done button clicked - destroy window to exit message loop
            DestroyWindow(hwnd);
        }
        return 0;
    case WM_CLOSE:
        // Allow closing after install completes (when Done button is enabled)
        if (hDone && IsWindowEnabled(hDone)) {
            DestroyWindow(hwnd);
        }
        // Otherwise ignore close during installation
        return 0;
    case WM_DESTROY:
        if (hAnim) {
            KillTimer(hAnim, 0xBEEF);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

// Parse winget output line-by-line and detect phases
static void ProcessWingetOutput(const std::string& line, HWND hwnd, HWND hProg, HWND hAnim, HWND hStatus, 
                                 std::string& currentPhase, std::string& currentPackage) {
    // Trim whitespace
    std::string trimmed = line;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t' || trimmed.front() == '\r'))
        trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t' || trimmed.back() == '\r' || trimmed.back() == '\n'))
        trimmed.pop_back();
    
    if (trimmed.empty()) return;
    
    // Detect phase changes
    if (trimmed.find("Found ") == 0) {
        // Extract package name
        size_t start = 6; // "Found "
        size_t bracket = trimmed.find('[', start);
        if (bracket != std::string::npos) {
            currentPackage = trimmed.substr(start, bracket - start);
            // Trim trailing space
            while (!currentPackage.empty() && currentPackage.back() == ' ')
                currentPackage.pop_back();
        }
    }
    else if (trimmed.find("Downloading") == 0 || trimmed.find("Download started") != std::string::npos) {
        currentPhase = "download";
        // Show progress bar, hide animation
        ShowWindow(hProg, SW_SHOW);
        ShowWindow(hAnim, SW_HIDE);
        
        std::wstring statusText = L"Downloading " + Utf8ToWide(currentPackage);
        SetWindowTextW(hStatus, statusText.c_str());
        
        // Reset progress bar for download (0-100%)
        SendMessageW(hProg, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessageW(hProg, PBM_SETPOS, 0, 0);
    }
    else if (trimmed.find("Download complete") != std::string::npos || 
             trimmed.find("Installing") == 0 || 
             trimmed.find("Successfully installed") == 0) {
        if (currentPhase != "install" && trimmed.find("Installing") == 0) {
            currentPhase = "install";
            // Hide progress bar, show animation
            ShowWindow(hProg, SW_HIDE);
            ShowWindow(hAnim, SW_SHOW);
            
            // Reset animation to start position
            g_animFrame = 0;
            
            std::wstring statusText = L"Installing " + Utf8ToWide(currentPackage);
            SetWindowTextW(hStatus, statusText.c_str());
        }
    }
    
    // Parse download progress (e.g., "10 MB / 100 MB")
    if (currentPhase == "download") {
        std::regex mbRe(R"((\d+)\s*(MB|MiB)\s*/\s*(\d+)\s*(MB|MiB))", std::regex::icase);
        std::smatch match;
        if (std::regex_search(trimmed, match, mbRe)) {
            try {
                int current = std::stoi(match[1].str());
                int total = std::stoi(match[3].str());
                if (total > 0) {
                    int percent = (current * 100) / total;
                    SendMessageW(hProg, PBM_SETPOS, percent, 0);
                }
            } catch (...) {}
        }
    }
}

// Filter output lines - return true if line should be displayed
static bool ShouldDisplayLine(const std::string& line) {
    std::string trimmed = line;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t' || trimmed.front() == '\r'))
        trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == '\t' || trimmed.back() == '\r' || trimmed.back() == '\n'))
        trimmed.pop_back();
    
    if (trimmed.empty()) return false;
    
    // Skip download progress lines (we handle them separately)
    if (trimmed.find("Downloading") == 0 && trimmed.find("MB") != std::string::npos) {
        return false;
    }
    
    // Skip spinner/progress characters
    static const std::regex spinnerRe(R"(^[\s\-\\\|\/\._\(\)\[\]│█▓▒]+$)");
    if (std::regex_match(trimmed, spinnerRe)) {
        return false;
    }
    
    // Skip large block bars
    static const std::regex blockBarRe(R"([█▓▒]{8,})");
    if (std::regex_search(trimmed, blockBarRe)) {
        return false;
    }
    
    return true;
}

bool ShowInstallDialog(HWND hParent, const std::vector<std::string>& packageIds, const std::wstring& doneButtonText) {
    if (packageIds.empty()) return false;
    
    // Store done button text in static variable for window procedure
    g_doneButtonText = doneButtonText;
    
    // Register dialog class
    const wchar_t CLASS_NAME[] = L"WinUpdateInstallDialog";
    
    WNDCLASSW wc = {};
    wc.lpfnWndProc = InstallDlgProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    
    RegisterClassW(&wc);
    
    // Create modal dialog
    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"Installing Updates",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 480,
        hParent, NULL, GetModuleHandleW(NULL), NULL);
    
    if (!hwnd) return false;
    
    // Get dialog controls
    HWND hOverallStatus = GetDlgItem(hwnd, 1002);
    HWND hStatus = NULL;
    HWND hProg = NULL;
    HWND hAnim = NULL;
    HWND hOut = NULL;
    HWND hDone = GetDlgItem(hwnd, 1001);
    
    // Find controls by iterating children
    struct ControlHandles {
        HWND* hProg;
        HWND* hOut;
        HWND* hAnim;
        HWND* hStatus;
    };
    
    ControlHandles handles{&hProg, &hOut, &hAnim, &hStatus};
    
    EnumChildWindows(hwnd, [](HWND hChild, LPARAM lParam) -> BOOL {
        wchar_t className[256];
        GetClassNameW(hChild, className, 256);
        
        auto* handles = (ControlHandles*)lParam;
        
        if (wcscmp(className, L"msctls_progress32") == 0) {
            *handles->hProg = hChild;
        } else if (wcscmp(className, L"Edit") == 0) {
            *handles->hOut = hChild;
        } else if (wcscmp(className, L"Static") == 0) {
            RECT rc;
            GetWindowRect(hChild, &rc);
            int height = rc.bottom - rc.top;
            // Animation overlay is about 28px high
            if (height >= 20 && height <= 35) {
                *handles->hAnim = hChild;
            } else if (height < 25 && *handles->hStatus == NULL) {
                *handles->hStatus = hChild;
            }
        }
        return TRUE;
    }, (LPARAM)&handles);
    
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    
    // Generate unique temp file name for output only (no batch file)
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring outputFile = std::wstring(tempPath) + L"winupdate_output_" + std::to_wstring(GetTickCount()) + L".txt";
    
    // Start winget in background thread with UAC elevation
    std::thread([hwnd, hOut, hProg, hStatus, hDone, hAnim, hOverallStatus, packageIds, outputFile]() {
        // Build winget command
        std::wstring cmd = L"winget upgrade";
        for (const auto& id : packageIds) {
            cmd += L" --id \"" + Utf8ToWide(id) + L"\"";
        }
        cmd += L" --accept-package-agreements --accept-source-agreements";
        
        // Build PowerShell command with output redirection
        std::wstring fullCmd = L"-NoProfile -ExecutionPolicy Bypass -Command \"& {" + cmd + L"} *>&1 | Out-File -FilePath '" + outputFile + L"' -Encoding UTF8\"";
        
        // Run PowerShell elevated with UAC
        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        sei.hwnd = hwnd;
        sei.lpVerb = L"runas";
        sei.lpFile = L"powershell.exe";
        sei.lpParameters = fullCmd.c_str();
        sei.nShow = SW_HIDE;
        
        if (!ShellExecuteExW(&sei) || !sei.hProcess) {
            // User cancelled UAC or elevation failed
            SendMessageW(hStatus, WM_SETTEXT, 0, (LPARAM)L"Installation cancelled or failed to elevate");
            EnableWindow(hDone, TRUE);
            DeleteFileW(outputFile.c_str());
            return;
        }
        
        // Read output file progressively while process runs
        std::string currentPhase;
        std::string currentPackage;
        int packageIndex = 0;
        std::streampos lastPos = 0;
        
        while (true) {
            DWORD waitResult = WaitForSingleObject(sei.hProcess, 500);
            
            // Try to read new content from output file
            std::ifstream outFile(outputFile.c_str(), std::ios::binary);
            if (outFile) {
                outFile.seekg(lastPos);
                std::string newContent;
                std::getline(outFile, newContent, '\0');
                lastPos = outFile.tellg();
                outFile.close();
                
                if (!newContent.empty()) {
                    // Process each line
                    std::istringstream iss(newContent);
                    std::string line;
                    while (std::getline(iss, line)) {
                        ProcessWingetOutput(line, hwnd, hProg, hAnim, hStatus, currentPhase, currentPackage);
                        
                        // Append to output window
                        if (ShouldDisplayLine(line)) {
                            std::wstring wline = Utf8ToWide(line) + L"\r\n";
                            int len = GetWindowTextLengthW(hOut);
                            SendMessageW(hOut, EM_SETSEL, len, len);
                            SendMessageW(hOut, EM_REPLACESEL, FALSE, (LPARAM)wline.c_str());
                            SendMessageW(hOut, EM_SCROLLCARET, 0, 0);
                        }
                    }
                }
            }
            
            if (waitResult == WAIT_OBJECT_0) break;
        }
        
        CloseHandle(sei.hProcess);
        
        // Read any remaining output
        std::ifstream finalFile(outputFile.c_str(), std::ios::binary);
        if (finalFile) {
            finalFile.seekg(lastPos);
            std::string remaining((std::istreambuf_iterator<char>(finalFile)), std::istreambuf_iterator<char>());
            if (!remaining.empty()) {
                std::wstring wremaining = Utf8ToWide(remaining);
                int len = GetWindowTextLengthW(hOut);
                SendMessageW(hOut, EM_SETSEL, len, len);
                SendMessageW(hOut, EM_REPLACESEL, FALSE, (LPARAM)wremaining.c_str());
            }
            finalFile.close();
        }
        
        // ALWAYS clean up temp file (no batch file to clean)
        DeleteFileW(outputFile.c_str());
        
        // Hide animation, show completion
        ShowWindow(hAnim, SW_HIDE);
        SetWindowTextW(hStatus, L"Installation complete!");
        EnableWindow(hDone, TRUE);
        
        // Append completion message
        std::wstring completionMsg = L"\r\n\r\n=== Installation Complete ===\r\n";
        completionMsg += std::to_wstring(packageIds.size()) + L" package(s) processed.\r\n";
        int curLen = GetWindowTextLengthW(hOut);
        SendMessageW(hOut, EM_SETSEL, curLen, curLen);
        SendMessageW(hOut, EM_REPLACESEL, FALSE, (LPARAM)completionMsg.c_str());
        SendMessageW(hOut, EM_SCROLLCARET, 0, 0);
    }).detach();
    
    // Modal message loop
    MSG msg = {};
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.hwnd == hwnd || IsChild(hwnd, msg.hwnd)) {
            if (msg.message == WM_QUIT) break;
            if (!IsDialogMessageW(hwnd, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        } else {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        
        // Check if dialog was closed
        if (!IsWindow(hwnd)) break;
    }
    
    DestroyWindow(hwnd);
    UnregisterClassW(CLASS_NAME, GetModuleHandleW(NULL));
    
    return true;
}

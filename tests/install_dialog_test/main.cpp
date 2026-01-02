#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <cmath>
#include <fstream>
#include <sstream>
#include <windowsx.h>
#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

#pragma comment(lib, "comctl32.lib")

static const int ID_LIST = 101;
static const int ID_BTN_START = 102;
static const int ID_PROGRESS = 103;
static const int ID_LABEL = 104;

static HWND g_hList = NULL;
static HWND g_hBtn = NULL;
static HWND g_hProgress = NULL;
static HWND g_hLabel = NULL;
static HFONT g_hBrailleFont = NULL;

static int g_phase = 0; // 0 idle, 1 download, 2 install
static int g_elapsedMs = 0;
static int g_tickMs = 200; // 200 ms per frame as requested
// larger groove so a braille glyph and fallback dots fit comfortably
static RECT g_animRect = {12, 190, 12 + 460, 190 + 200};
static int g_spin_state = 0;

std::vector<std::wstring> LoadApps(const wchar_t *fn) {
    std::vector<std::wstring> out;
    // Read as UTF-8 and convert to wide strings to avoid locale issues
    std::ifstream ifs(fn, std::ios::binary);
    if (!ifs) return out;
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        // trim trailing CR
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // convert UTF-8 to wide
        int needed = MultiByteToWideChar(CP_UTF8, 0, line.c_str(), (int)line.size(), NULL, 0);
        if (needed > 0) {
            std::wstring w(needed, 0);
            MultiByteToWideChar(CP_UTF8, 0, line.c_str(), (int)line.size(), &w[0], needed);
            out.push_back(w);
        } else {
            // fallback: simple widen
            std::wstring w(line.begin(), line.end());
            out.push_back(w);
        }
    }
    return out;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icce{sizeof(icce), ICC_PROGRESS_CLASS};
        InitCommonControlsEx(&icce);
        // create a ListView with checkboxes so multiple apps can be selected
        INITCOMMONCONTROLSEX icce2{sizeof(icce2), ICC_LISTVIEW_CLASSES};
        InitCommonControlsEx(&icce2);
        g_hList = CreateWindowW(WC_LISTVIEWW, NULL, WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | WS_BORDER | WS_VSCROLL,
            12, 12, 460, 120, hwnd, (HMENU)ID_LIST, GetModuleHandle(NULL), NULL);
        ListView_SetExtendedListViewStyle((HWND)g_hList, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT);
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.cx = 440;
        col.pszText = (LPWSTR)L"Application";
        ListView_InsertColumn((HWND)g_hList, 0, &col);
        g_hBtn = CreateWindowW(L"BUTTON", L"Start", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            12, 140, 100, 30, hwnd, (HMENU)ID_BTN_START, GetModuleHandle(NULL), NULL);
        g_hProgress = CreateWindowW(PROGRESS_CLASSW, NULL, WS_CHILD | WS_VISIBLE,
            12, 190, 460, 18, hwnd, (HMENU)ID_PROGRESS, GetModuleHandle(NULL), NULL);
        SendMessageW(g_hProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 1000));
        SendMessageW(g_hProgress, PBM_SETPOS, 0, 0);
        g_hLabel = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
            12, 220, 460, 24, hwnd, (HMENU)ID_LABEL, GetModuleHandle(NULL), NULL);
        // create a font that contains braille patterns (try Segoe UI Symbol then Emoji)
        // use 12px size as requested
        g_hBrailleFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            VARIABLE_PITCH, L"Segoe UI Symbol");
        if (!g_hBrailleFont) {
            g_hBrailleFont = CreateFontW(-12, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                VARIABLE_PITCH, L"Segoe UI Emoji");
        }

        // prepare a temporary winget output file with hardcoded entries, then load it
        wchar_t tempPath[MAX_PATH+1] = {0};
        wchar_t tempFile[MAX_PATH+1] = {0};
        if (GetTempPathW(MAX_PATH, tempPath)) {
            PathCombineW(tempFile, tempPath, L"winget_output_test.txt");
            // write UTF-8 lines to temp file
            std::ofstream ofs;
            // convert wide tempFile to UTF-8 narrow for ofstream open
            int n = WideCharToMultiByte(CP_UTF8, 0, tempFile, -1, NULL, 0, NULL, NULL);
            std::string utf8path(n, '\0');
            WideCharToMultiByte(CP_UTF8, 0, tempFile, -1, &utf8path[0], n, NULL, NULL);
            ofs.open(utf8path.c_str(), std::ios::binary);
            if (ofs) {
                // hardcoded example entries (UTF-8)
                ofs << "Mozilla.Thunderbird\r\n";
                ofs << "Notepad++.Notepad++\r\n";
                ofs << "KhronosGroup.VulkanSDK 1.4.335.0\r\n";
                ofs << "Microsoft.Teams\r\n";
                ofs.close();
                auto apps = LoadApps(tempFile);
                int idx = 0;
                for (auto &a : apps) {
                    LVITEMW itm{};
                    itm.mask = LVIF_TEXT;
                    itm.iItem = idx++;
                    itm.iSubItem = 0;
                    itm.pszText = (LPWSTR)a.c_str();
                    ListView_InsertItem((HWND)g_hList, &itm);
                }
            }
        }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == ID_BTN_START) {
            // gather checked items from ListView
            int count = ListView_GetItemCount((HWND)g_hList);
            std::vector<int> checked;
            for (int i = 0; i < count; ++i) {
                if (ListView_GetCheckState((HWND)g_hList, i)) checked.push_back(i);
            }
            if (checked.empty()) {
                MessageBoxW(hwnd, L"Select at least one app (check the boxes).", L"Test", MB_OK | MB_ICONINFORMATION);
                break;
            }
            // For this test harness we just start the simulated download/install for the checked set
            g_phase = 1;
            g_elapsedMs = 0;
            g_spin_state = 0;
            // hide native progress, use spinner animation instead
            ShowWindow(g_hProgress, SW_HIDE);
            // show which apps will be processed
            wchar_t buf[128];
            swprintf(buf, _countof(buf), L"Processing %d app(s)...", (int)checked.size());
            SetWindowTextW(g_hLabel, buf);
            EnableWindow(g_hBtn, FALSE);
            SetTimer(hwnd, 1, g_tickMs, NULL);
        }
        break;
    }
    case WM_TIMER: {
        if (wParam == 1) {
            g_elapsedMs += g_tickMs;
            if (g_phase == 1) {
                // download 10s
                int total = 10000;
                // advance spinner state (use 6-frame braille spinner)
                g_spin_state = (g_spin_state + 1) % 6;
                InvalidateRect(hwnd, &g_animRect, TRUE);
                if (g_elapsedMs >= total) {
                    // switch to install
                    g_phase = 2;
                    g_elapsedMs = 0;
                    // hide progress bar and show install label with dots
                    // clear label and start spinner in the same groove as progress bar
                    SetWindowTextW(g_hLabel, L"");
                    g_spin_state = 0;
                    InvalidateRect(hwnd, &g_animRect, TRUE);
                }
            } else if (g_phase == 2) {
                // install animation for 10s
                int total = 10000;
                // animate spinner during install (6-frame)
                g_spin_state = (g_spin_state + 1) % 6;
                InvalidateRect(hwnd, &g_animRect, FALSE);
                if (g_elapsedMs >= total) {
                    KillTimer(hwnd, 1);
                    g_phase = 0;
                    SetWindowTextW(g_hLabel, L"Install complete.");
                    EnableWindow(g_hBtn, TRUE);
                    // ensure final state repainted
                    InvalidateRect(hwnd, &g_animRect, TRUE);
                }
            }
        }
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (g_phase == 1 || g_phase == 2) {
            // draw spinner inside g_animRect for both download and install phases
            RECT r = g_animRect;
            // draw groove background
            HBRUSH hbg = CreateSolidBrush(GetSysColor(COLOR_3DFACE));
            FillRect(hdc, &r, hbg);
            DeleteObject(hbg);
            // draw inner light background to mimic progress groove
            RECT inner = r;
            // add more vertical padding to avoid clipping of glyph/dots
            InflateRect(&inner, -6, -12);
            HBRUSH hinner = CreateSolidBrush(RGB(245,245,245));
            FillRect(hdc, &inner, hinner);
            DeleteObject(hinner);

            // Draw a large braille glyph spinner centered in the groove.
            // Braille codepoints start at U+2800; bits 0..7 map to dots 1..8.
                // Draw a braille glyph spinner (12px) centered in the groove.
                // Also draw a small manual 2x3 dot fallback to ensure visibility on systems
                // where the braille glyph doesn't render clearly at small sizes.
                // Standard braille is 2x3 (dots 1..6). We'll animate 6 positions clockwise.
                const int spinner_masks[6] = { 0x01, 0x08, 0x10, 0x20, 0x04, 0x02 };
                int mask = spinner_masks[g_spin_state % 6];
                wchar_t glyph[2] = { (wchar_t)(0x2800 + mask), 0 };

                // Attempt glyph drawing first — rotate glyph 90deg clockwise using world transform
                HFONT hOldFont = (HFONT)SelectObject(hdc, g_hBrailleFont);
                SetBkMode(hdc, TRANSPARENT);
                // compute center of inner rect for rotation pivot
                int cx = (inner.left + inner.right) / 2;
                int cy = (inner.top + inner.bottom) / 2;
                // Save DC then enable advanced mode and set a 90deg clockwise transform
                int save = SaveDC(hdc);
                SetGraphicsMode(hdc, GM_ADVANCED);
                XFORM xform;
                // 90deg clockwise: [0 1; -1 0]
                xform.eM11 = 0.0f;
                xform.eM12 = 1.0f;
                xform.eM21 = -1.0f;
                xform.eM22 = 0.0f;
                // translation so rotation pivots about (cx,cy): D = -M*c + c
                xform.eDx = - (xform.eM11 * (FLOAT)cx + xform.eM21 * (FLOAT)cy) + (FLOAT)cx;
                xform.eDy = - (xform.eM12 * (FLOAT)cx + xform.eM22 * (FLOAT)cy) + (FLOAT)cy;
                SetWorldTransform(hdc, &xform);
                // draw darker shadow behind glyph (offset 1,1)
                SetTextColor(hdc, RGB(100,0,0));
                RECT shadow = inner;
                OffsetRect(&shadow, 1, 1);
                DrawTextW(hdc, glyph, 1, &shadow, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                // main glyph (RAL 3020 approximation)
                SetTextColor(hdc, RGB(204,4,0));
                DrawTextW(hdc, glyph, 1, &inner, DT_SINGLELINE | DT_CENTER | DT_VCENTER);
                // restore DC (undo world transform and graphics mode)
                RestoreDC(hdc, save);
                SelectObject(hdc, hOldFont);

                // Manual 2x3 dot fallback (very small, subtle) centered beneath glyph
                const int cols = 2, rows = 3;
                int cellW = inner.right - inner.left;
                int cellH = inner.bottom - inner.top;
                int dotDiameter = 7; // increased ~10% from 6 -> 7 for more emphasis
                int dotSpacingX = dotDiameter + 4;
                int dotSpacingY = dotDiameter + 4;
                int totalWidth = (cols - 1) * dotSpacingX + cols * dotDiameter;
                int totalHeight = (rows - 1) * dotSpacingY + rows * dotDiameter;
                int startX = inner.left + (cellW - totalWidth) / 2;
                int startY = inner.top + (cellH - totalHeight) / 2 + 6; // small nudge

                // clockwise order mapping for visual motion (2x3 braille indices)
                // bit indices: col0,row0=0 (dot1), col0,row1=1 (dot2), col0,row2=2 (dot3)
                //              col1,row0=3 (dot4), col1,row1=4 (dot5), col1,row2=5 (dot6)
                // clockwise order starting at top-left: 0,3,4,5,2,1
                const int order[6] = { 0, 3, 4, 5, 2, 1 };
                int active_idx = g_spin_state % 6;
                HBRUSH hOn = CreateSolidBrush(RGB(204,4,0));
                HBRUSH hNear = CreateSolidBrush(RGB(255,150,150));
                HBRUSH hOff = CreateSolidBrush(RGB(240,240,240));
                // subtle darker shadow for active/near dots (offset by 1px)
                HBRUSH hShadow = CreateSolidBrush(RGB(100,0,0));

                // Use NULL pen to avoid outlining the filled dots (remove rings)
                HPEN hOldPen = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));
                for (int r = 0; r < rows; ++r) {
                    for (int c = 0; c < cols; ++c) {
                        int bitIndex = -1;
                        if (c == 0) {
                            if (r == 0) bitIndex = 0; else if (r == 1) bitIndex = 1; else if (r == 2) bitIndex = 2;
                        } else {
                            if (r == 0) bitIndex = 3; else if (r == 1) bitIndex = 4; else if (r == 2) bitIndex = 5;
                        }
                        int pos = 0;
                        for (int k = 0; k < 6; ++k) if (order[k] == bitIndex) { pos = k; break; }
                        int dx = startX + c * dotSpacingX;
                        int dy = startY + r * dotSpacingY;
                        // rotate this fallback dot position 90deg clockwise about inner center
                        // rotation: x' = cx + (y - cy); y' = cy - (x - cx)
                        int rx = cx + (dy - cy);
                        int ry = cy - (dx - cx);
                        dx = rx; dy = ry;
                        int dist = (pos - active_idx + 6) % 6;
                        HBRUSH use = hOff;
                        if (dist == 0) use = hOn; else if (dist == 1 || dist == 5) use = hNear;
                        // draw subtle shadow for active/near dots first
                        if (dist == 0 || dist == 1 || dist == 5) {
                            HBRUSH prevSh = (HBRUSH)SelectObject(hdc, hShadow);
                            Ellipse(hdc, dx + 1, dy + 1, dx + dotDiameter + 1, dy + dotDiameter + 1);
                            SelectObject(hdc, prevSh);
                        }
                        HBRUSH prev = (HBRUSH)SelectObject(hdc, use);
                        Ellipse(hdc, dx, dy, dx + dotDiameter, dy + dotDiameter);
                        SelectObject(hdc, prev);
                    }
                }
            // restore previous pen
            SelectObject(hdc, hOldPen);
            DeleteObject(hOn); DeleteObject(hNear); DeleteObject(hOff); DeleteObject(hShadow);
        }
        EndPaint(hwnd, &ps);
        break;
    }
    case WM_DESTROY:
        if (g_hBrailleFont) { DeleteObject(g_hBrailleFont); g_hBrailleFont = NULL; }
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

// Main entry for the test app (renamed to avoid confusion with the project's main)
int WINAPI TestAppMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"InstallTestClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Install Dialog Test", WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 420, NULL, NULL, hInstance, NULL);
    if (!hwnd) return 0;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}

// Keep the standard WinMain/wWinMain entrypoints as thin wrappers that call TestAppMain
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nShowCmd) {
    return TestAppMain(hInstance, hPrevInstance, pCmdLine, nShowCmd);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd) {
    return TestAppMain(hInstance, hPrevInstance, GetCommandLineW(), nShowCmd);
}

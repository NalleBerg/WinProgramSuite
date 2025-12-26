#include <windows.h>
#include <string>
#include <thread>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR pCmdLine, int) {
    int x = 100, y = 100;
    std::wstring title = L"Probe Message";
    std::wstring text = L"Probe: do you see this message?";
    // parse command-line: expected format: <x> <y> [title] [text]
    int argc = 0; wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        if (argc >= 3) {
            try { x = std::stoi(argv[1]); } catch(...) {}
            try { y = std::stoi(argv[2]); } catch(...) {}
        }
        if (argc >= 4) title = argv[3];
        if (argc >= 5) text = argv[4];
        LocalFree(argv);
    }

    const wchar_t CLASSNAME[] = L"MsgBoxProbeClass";
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = CLASSNAME;
    RegisterClassW(&wc);

    // create small owner window at x,y; MessageBox will center over it
    HWND hOwner = CreateWindowExW(WS_EX_TOPMOST, CLASSNAME, L"", WS_POPUP,
        x, y, 10, 10, NULL, NULL, GetModuleHandle(NULL), NULL);
    if (hOwner) {
        // make visible (so MessageBox centers correctly)
        ShowWindow(hOwner, SW_SHOW);
        UpdateWindow(hOwner);
    }

    // small delay to ensure window manager has positioned it
    std::this_thread::sleep_for(std::chrono::milliseconds(80));

    MessageBoxW(hOwner, text.c_str(), title.c_str(), MB_OK | MB_SETFOREGROUND | MB_TOPMOST);

    if (hOwner) DestroyWindow(hOwner);
    return 0;
}

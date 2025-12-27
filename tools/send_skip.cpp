#include <windows.h>
#include <string>
#include <iostream>
int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        std::wcerr << L"Usage: send_skip.exe \"App Name\" Version\n";
        return 1;
    }
    std::wstring app = argv[1];
    std::wstring ver = argv[2];
    HWND h = FindWindowW(L"WinUpdateClass", NULL);
    if (!h) {
        std::wcerr << L"Main window not found\n";
        return 2;
    }
    // convert to UTF-8
    int na = WideCharToMultiByte(CP_UTF8, 0, app.c_str(), -1, NULL, 0, NULL, NULL);
    int nv = WideCharToMultiByte(CP_UTF8, 0, ver.c_str(), -1, NULL, 0, NULL, NULL);
    std::string a(na, '\0'); std::string v(nv, '\0');
    WideCharToMultiByte(CP_UTF8,0,app.c_str(),-1,&a[0],na,NULL,NULL);
    WideCharToMultiByte(CP_UTF8,0,ver.c_str(),-1,&v[0],nv,NULL,NULL);
    std::string payload = "WUP_SKIP\n" + a + "\n" + v + "\n";
    COPYDATASTRUCT cds{}; cds.dwData = 0x57475053; cds.cbData = (DWORD)(payload.size()+1); cds.lpData = (PVOID)payload.c_str();
    LRESULT r = SendMessageA(h, WM_COPYDATA, (WPARAM)NULL, (LPARAM)&cds);
    std::cout << "SendMessage returned " << (long)r << "\n";
    return 0;
}

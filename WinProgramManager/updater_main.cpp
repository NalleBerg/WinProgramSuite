// NOTE: This file is shared between WinProgramUpdater.exe (silent/GUI) and 
// WinProgramUpdaterConsole.exe (with console output). If you modify this file,
// both executables need to be rebuilt. They differ only in their subsystem:
// - WinProgramUpdater.exe: WIN32 subsystem (no console window)
// - WinProgramUpdaterConsole.exe: CONSOLE subsystem (shows output)

#include "WinProgramUpdater.h"
#include "db_path_utils.h"
#include <windows.h>
#include <iostream>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    (void)hInstance;
    (void)hPrevInstance;
    (void)pCmdLine;
    (void)nCmdShow;
    
    // Get database path from ProgramData (placed there by the installer)
    std::wstring dbPath = GetDatabasePath();
    if (dbPath.empty()) {
        MessageBoxW(NULL, L"Failed to get database path from ProgramData.", 
                    L"Database Error", MB_ICONERROR | MB_OK);
        return 1;
    }
    
    // Get executable directory for setting working directory
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeDir;
    size_t lastSlash = std::wstring(exePath).find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        exeDir = std::wstring(exePath).substr(0, lastSlash + 1);
    }
    
    // Set working directory to executable location for portable relative paths
    if (!exeDir.empty()) {
        SetCurrentDirectoryW(exeDir.c_str());
    }
    
    // Run updater
    WinProgramUpdater updater(dbPath);
    UpdateStats stats;
    
    bool success = updater.UpdateDatabase(stats);
    
    return success ? 0 : 1;
}

// Alternative console entry point for debugging
#ifdef _CONSOLE
int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;
    
    // Get database path from ProgramData (placed there by the installer)
    std::wstring dbPath = GetDatabasePath();
    if (dbPath.empty()) {
        std::wcerr << L"Failed to get database path from ProgramData." << std::endl;
        return 1;
    }
    
    // Get executable directory for setting working directory
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeDir;
    size_t lastSlash = std::wstring(exePath).find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        exeDir = std::wstring(exePath).substr(0, lastSlash + 1);
    }
    
    // Set working directory to executable location for portable relative paths
    if (!exeDir.empty()) {
        SetCurrentDirectoryW(exeDir.c_str());
    }
    
    std::wcout << L"Starting WinProgramUpdater..." << std::endl;
    std::wcout << L"Database: " << dbPath << std::endl;
    
    WinProgramUpdater updater(dbPath);
    UpdateStats stats;
    
    std::wcout << L"\nUpdating database..." << std::endl;
    bool success = updater.UpdateDatabase(stats);
    
    if (success) {
        std::wcout << L"\n✓ Update complete!" << std::endl;
        std::wcout << L"  Packages added: " << stats.packagesAdded << std::endl;
        std::wcout << L"  Packages removed: " << stats.packagesRemoved << std::endl;
        std::wcout << L"  Tags from winget: " << stats.tagsFromWinget << std::endl;
        std::wcout << L"  Tags from inference: " << stats.tagsFromInference << std::endl;
        std::wcout << L"  Tags from correlation: " << stats.tagsFromCorrelation << std::endl;
        std::wcout << L"  Uncategorized: " << stats.uncategorized << std::endl;
        
        std::wcout << L"\nLog written to %APPDATA%\\WinProgramManager\\log\\WinProgramUpdater.log" << std::endl;
    } else {
        std::wcout << L"\n✗ Update failed!" << std::endl;
        std::wcout << L"Log written to %APPDATA%\\WinProgramManager\\log\\WinProgramUpdater.log" << std::endl;
        return 1;
    }
    
    return 0;
}
#endif

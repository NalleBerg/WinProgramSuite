#pragma once
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <filesystem>

// Get the ProgramData folder path for WinProgramManager (shared for all users).
// The installer creates C:\ProgramData\WinProgramManager and places the DB there,
// so no directory creation is needed at runtime.
inline std::wstring GetAppDataPath() {
    wchar_t* programDataPath = nullptr;
    HRESULT hr = SHGetKnownFolderPath(FOLDERID_ProgramData, 0, NULL, &programDataPath);

    std::wstring result;
    if (SUCCEEDED(hr) && programDataPath) {
        result = programDataPath;
        CoTaskMemFree(programDataPath);

        if (result.back() != L'\\' && result.back() != L'/') {
            result += L"\\";
        }
        result += L"WinProgramManager";
    }

    return result;
}

// Get the full path to the main database in ProgramData (shared for all users)
inline std::wstring GetDatabasePath() {
    std::wstring programDataPath = GetAppDataPath();
    if (programDataPath.empty()) {
        return L"";
    }

    if (programDataPath.back() != L'\\' && programDataPath.back() != L'/') {
        programDataPath += L"\\";
    }

    return programDataPath + L"WinProgramManager.db";
}

// Get the full path to the search database in ProgramData (shared for all users)
inline std::wstring GetSearchDatabasePath() {
    std::wstring programDataPath = GetAppDataPath();
    if (programDataPath.empty()) {
        return L"";
    }

    if (programDataPath.back() != L'\\' && programDataPath.back() != L'/') {
        programDataPath += L"\\";
    }

    return programDataPath + L"WinProgramsSearch.db";
}


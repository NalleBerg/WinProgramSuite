#ifndef SEARCH_H
#define SEARCH_H

#include <windows.h>
#include <string>

// Forward declarations
extern HWND g_hTagTree;
extern HWND g_hAppList;
extern std::wstring g_selectedTag;

// Helper functions
std::wstring Trim(const std::wstring& str);

// Data loading functions
void LoadTags(const std::wstring& filter = L"");
void LoadApps(const std::wstring& tag, const std::wstring& filter = L"");

#endif // SEARCH_H

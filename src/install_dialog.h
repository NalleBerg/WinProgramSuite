#pragma once
#include <windows.h>
#include <string>
#include <vector>

// Show modal install dialog for the given package IDs
// Returns true if install completed (user clicked Done), false if cancelled
bool ShowInstallDialog(HWND hParent, const std::vector<std::string>& packageIds, const std::wstring& doneButtonText = L"Done!");

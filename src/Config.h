#pragma once
#include <windows.h>
#include <string>

// Show the configuration dialog
// Returns true if settings changed
bool ShowConfigDialog(HWND parent, const std::string &currentLocale);

// Check if "Add to systray now" button was clicked (resets flag after reading)
bool WasAddToTrayNowClicked();

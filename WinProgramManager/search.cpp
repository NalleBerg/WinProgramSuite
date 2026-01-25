#include "search.h"
#include <commctrl.h>

// Trim whitespace from string
std::wstring Trim(const std::wstring& str) {
    size_t first = str.find_first_not_of(L" \t\n\r");
    if (first == std::wstring::npos) return L"";
    size_t last = str.find_last_not_of(L" \t\n\r");
    return str.substr(first, (last - first + 1));
}

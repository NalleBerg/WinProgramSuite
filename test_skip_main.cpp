#include <windows.h>
#include <string>
#include "src/skip_confirm_dialog.h"
#include "src/logging.h"

int main() {
    AppendLog("[test_skip] starting test exe\n");
    bool r = ShowSkipConfirm(NULL, L"Vulkan SDK", L"1.2.3");
    AppendLog(std::string("[test_skip] ShowSkipConfirm returned ") + (r?"true":"false") + "\n");
    MessageBoxW(NULL, r?L"Returned true":L"Returned false", L"Test", MB_OK);
    return 0;
}

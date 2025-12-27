#include <iostream>
#include "src/skip_update.h"
#include "src/logging.h"

int main() {
    AppendLog("[test_add_skip] starting\n");
    bool ok = AddSkippedEntry("test.sample.pkg", "9.9.9");
    std::cout << "AddSkippedEntry returned " << (ok?"true":"false") << "\n";
    AppendLog(std::string("[test_add_skip] AddSkippedEntry returned ") + (ok?"true":"false") + "\n");
    return ok?0:1;
}

Test program for the install dialog simulation

Files:
- main.cpp : Win32 test program that reads apps from `apps.txt` and simulates a 10s download (progress bar) then a 10s install (5-dot animation).
- apps.txt : list of apps (one per line). Example provided.

Build (MinGW):

```powershell
g++ -std=c++17 main.cpp -o install_test.exe -lcomctl32 -mwindows
```

Run:

```powershell
cd tests\install_dialog_test
..\..\build_path_here\install_test.exe  # or use the compiled exe location
```

Usage:
- Select the app in the list and click Start.
- The progress bar will run for 10 seconds (download), then hide and an install animation will run for 10 seconds.
- When complete the label will show "Install complete." and Start will be re-enabled.

Notes:
- This test program is intentionally small and standalone; it does not call winget.
- If you want the UI to exactly match the app, we can port this dialog's logic into the main project after verification.

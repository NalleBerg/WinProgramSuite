# WinProgramUpdater

Automatic database updater for WinProgramManager. Runs silently in the background to keep the package database synchronized with the winget repository.

## Features

- **Fully Automated**: No user intervention required
- **Hidden Execution**: Runs without visible windows
- **Complete Update Pipeline**:
  1. Fetches current winget package list
  2. Adds new packages to database
  3. Removes deleted packages
  4. Filters out invalid numeric-only IDs
  5. Queries tags for untagged packages
  6. Applies name-based inference (45 patterns)
  7. Runs correlation analysis (66.67% threshold)
  8. Tags remaining packages as "uncategorized"

## Logging

**Location**: `%APPDATA%\WinUpdate\WinProgramUpdaterLog.txt`

**Format**:
```
Run time: 2026.01.18-14:30:45
Added: 15 items
Removed: 3 items

Run time: 2026.01.11-02:15:32
Added: 8 items
Removed: 1 items
```

**Log Rotation**: Automatically prunes entries older than 90 days.

## Usage

### Manual Execution
```cmd
WinProgramUpdater.exe
```

### Scheduled Task (Recommended)
Create a Windows scheduled task to run weekly:

```powershell
$action = New-ScheduledTaskAction -Execute "C:\Path\To\WinProgramUpdater.exe"
$trigger = New-ScheduledTaskTrigger -Weekly -DaysOfWeek Sunday -At 2:00AM
$settings = New-ScheduledTaskSettingsSet -Hidden -AllowStartIfOnBatteries
Register-ScheduledTask -TaskName "WinProgramManager Database Update" `
                       -Action $action `
                       -Trigger $trigger `
                       -Settings $settings `
                       -Description "Weekly update of WinProgramManager database"
```

## Building

```bash
cd WinProgramManager
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

Output: `build/WinProgramUpdater.exe`

## Dependencies

- **sqlite3.dll**: SQLite database library (included in `sqlite3/` directory)
- **winget**: Windows Package Manager (pre-installed on Windows 10/11)
- **Windows API**: Shell32, Ole32

## Tag Inference Patterns

The updater applies 45 keyword patterns across categories:
- **Hardware**: USB, Bluetooth, WiFi, HDMI, GPU, CPU
- **App Types**: Browser, Client, Server, Manager, Editor, Player
- **Functions**: Emulator, Driver, SDK, CLI, Testing, Debug
- **File Formats**: JSON, XML, YAML, CSV, SQL, HTML, PDF
- **Media**: Video, Audio, Image, Photo, Music
- **Categories**: Gaming, Security, Utilities, Backup

## Technical Details

- **Language**: C++17
- **Subsystem**: Windows GUI (no console)
- **Retry Logic**: 3 attempts with exponential backoff for winget queries
- **Encoding**: UTF-8 with Unicode support
- **Database**: SQLite3 with COLLATE NOCASE for case-insensitive matching

## Error Handling

- Silent failure - logs errors internally
- Continues on individual package query failures
- Skips malformed package IDs (numeric-only)
- Validates database integrity before updates

## Performance

Typical execution time: 30-60 minutes for full update cycle (depends on number of new packages and network speed).

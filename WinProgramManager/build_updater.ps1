# Build WinProgramUpdater.exe
# Compiles the hidden background updater

Write-Host "`n🔨 Building WinProgramUpdater..." -ForegroundColor Cyan

# Clean old build
if (Test-Path "build_updater") {
    Remove-Item -Recurse -Force "build_updater"
}

# Configure CMake
Write-Host "`n📋 Configuring CMake..." -ForegroundColor Yellow
cmake -B build_updater -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release

if ($LASTEXITCODE -ne 0) {
    Write-Host "`n❌ CMake configuration failed!" -ForegroundColor Red
    exit 1
}

# Build
Write-Host "`n🔧 Compiling..." -ForegroundColor Yellow
cmake --build build_updater --config Release

if ($LASTEXITCODE -ne 0) {
    Write-Host "`n❌ Build failed!" -ForegroundColor Red
    exit 1
}

Write-Host "`n✅ Build complete!" -ForegroundColor Green
Write-Host "   Output: build_updater\WinProgramUpdater.exe" -ForegroundColor White

# Copy dependencies
Write-Host "`n📦 Copying dependencies..." -ForegroundColor Yellow
Copy-Item "sqlite3\sqlite3.dll" "build_updater\" -Force
Copy-Item "WinProgramManager.db" "build_updater\" -Force -ErrorAction SilentlyContinue

Write-Host "`n✅ WinProgramUpdater is ready!" -ForegroundColor Green
Write-Host "   Run: .\build_updater\WinProgramUpdater.exe" -ForegroundColor White
Write-Host "   Log: %APPDATA%\WinUpdate\WinProgramUpdaterLog.txt" -ForegroundColor White

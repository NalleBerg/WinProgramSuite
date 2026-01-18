# Restore Missing Apps to Database
# This script compares winget packages with database entries and adds missing apps

param(
    [int]$BatchSize = 100,
    [switch]$DryRun,  # Show what would be added without actually adding
    [switch]$QuickMode  # Skip detailed metadata, just add basic info
)

$ErrorActionPreference = "Continue"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$PSDefaultParameterValues['*:Encoding'] = 'utf8'

$scriptDir = $PSScriptRoot
$dbPath = Join-Path $scriptDir "WinProgramManager.db"
$ignoreDbPath = Join-Path $scriptDir "WinProgramManagerIgnore.db"
$logPath = Join-Path $scriptDir "restore_missing_apps.log"

Write-Host "=== Restore Missing Apps ===" -ForegroundColor Cyan
Write-Host "Started: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan

if ($DryRun) {
    Write-Host "DRY RUN MODE: No changes will be made to database" -ForegroundColor Yellow
}

# Log function
function Write-Log {
    param([string]$Message, [string]$Color = "White")
    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $logMessage = "[$timestamp] $Message"
    Write-Host $logMessage -ForegroundColor $Color
    Add-Content -Path $logPath -Value $logMessage
}

# Check if tag is ignored
function Test-IgnoredTag {
    param([string]$Tag)
    
    if (-not (Test-Path $ignoreDbPath)) { return $false }
    
    $result = & sqlite3\sqlite3.exe $ignoreDbPath "SELECT COUNT(*) FROM ignored_tags WHERE tag_name = '$($Tag.Replace("'", "''"))' COLLATE NOCASE;"
    return [int]$result -gt 0
}

# Get package info from winget
function Get-PackageInfo {
    param([string]$PackageId)
    
    try {
        $info = winget show $PackageId --accept-source-agreements 2>$null | Out-String
        
        if ([string]::IsNullOrWhiteSpace($info)) {
            return $null
        }
        
        $package = @{
            package_id = $PackageId
            name = $null
            version = $null
            publisher = $null
            description = $null
            tags = @()
        }
        
        # Extract basic info
        if ($info -match 'Found\s+(.+?)\s+\[') { $package.name = $matches[1].Trim() }
        if ($info -match 'Version:\s+(.+)') { $package.version = $matches[1].Trim() }
        if ($info -match 'Publisher:\s+(.+)') { $package.publisher = $matches[1].Trim() }
        if ($info -match '(?s)Description:\s*\n(.*?)(?:\n[A-Z][a-z]+:|$)') { 
            $package.description = $matches[1].Trim() -replace '\s+', ' '
        }
        
        # Extract tags
        if ($info -match '(?s)Tags:\s*\n(.*?)(?:\n[A-Z][a-z]+:|$)') {
            $tagSection = $matches[1]
            $tags = ($tagSection -split '\n' | Where-Object {$_ -match '^\s+\S'} | ForEach-Object {$_.Trim()}) | Where-Object {$_}
            
            # Filter to valid tags
            foreach ($tag in $tags) {
                if ($tag -match '^[a-zA-Z0-9][a-zA-Z0-9._-]*$') {
                    $tagLower = $tag.ToLower()
                    if (-not (Test-IgnoredTag $tagLower)) {
                        $package.tags += $tagLower
                    }
                }
            }
        }
        
        return $package
    }
    catch {
        Write-Log "Error getting info for ${PackageId}: $_" "Red"
        return $null
    }
}

# Get or create category ID
function Get-CategoryId {
    param([string]$CategoryName)
    
    # Capitalize first letter
    $CategoryName = $CategoryName.Substring(0,1).ToUpper() + $CategoryName.Substring(1)
    
    $existingSql = "SELECT id FROM categories WHERE category_name = '$($CategoryName.Replace("'", "''"))' COLLATE NOCASE;"
    $categoryId = & sqlite3\sqlite3.exe $dbPath $existingSql
    
    if ([string]::IsNullOrWhiteSpace($categoryId)) {
        $insertSql = "INSERT INTO categories (category_name) VALUES ('$($CategoryName.Replace("'", "''"))'); SELECT last_insert_rowid();"
        $categoryId = & sqlite3\sqlite3.exe $dbPath $insertSql
    }
    
    return $categoryId
}

# Add package to database
function Add-PackageToDatabase {
    param($Package)
    
    if ($DryRun) {
        Write-Log "  [DRY RUN] Would add: $($Package.package_id) - $($Package.name)" "Yellow"
        return $true
    }
    
    try {
        # Insert app
        $insertSql = @"
INSERT INTO apps (package_id, name, version, publisher, description, processed_at)
VALUES (
    '$($Package.package_id.Replace("'", "''"))',
    '$($Package.name.Replace("'", "''"))',
    '$($Package.version.Replace("'", "''"))',
    '$($Package.publisher.Replace("'", "''"))',
    '$($Package.description.Replace("'", "''"))',
    datetime('now')
);
SELECT last_insert_rowid();
"@
        
        $appId = & sqlite3\sqlite3.exe $dbPath $insertSql
        
        if ([string]::IsNullOrWhiteSpace($appId)) {
            Write-Log "  Failed to insert app $($Package.package_id)" "Red"
            return $false
        }
        
        # Add categories from tags
        foreach ($tag in $Package.tags) {
            $categoryId = Get-CategoryId $tag
            
            $linkSql = "INSERT OR IGNORE INTO app_categories (app_id, category_id) VALUES ($appId, $categoryId);"
            & sqlite3\sqlite3.exe $dbPath $linkSql
        }
        
        Write-Log "  Added: $($Package.package_id) - $($Package.name) (ID: $appId, Categories: $($Package.tags.Count))" "Green"
        return $true
    }
    catch {
        Write-Log "  Error adding $($Package.package_id): $_" "Red"
        return $false
    }
}

# Main execution
Write-Log "Step 1: Getting all winget packages..." "Cyan"
Write-Log "This may take a few minutes..." "Yellow"

# Use winget export to get package list - much faster than search
$tempFile = [System.IO.Path]::GetTempFileName() + ".json"
Write-Log "Getting package list from winget source..."

try {
    # Get packages using a more reliable method
    $result = winget source export --source winget --output $tempFile 2>&1
    
    if (Test-Path $tempFile) {
        $jsonContent = Get-Content $tempFile -Raw | ConvertFrom-Json
        $allWingetPackages = $jsonContent.Sources[0].Packages | ForEach-Object { $_.PackageIdentifier } | Where-Object { $_ -match '[a-zA-Z]' }
        Remove-Item $tempFile -Force
    } else {
        # Fallback to search method
        Write-Log "Export failed, using search method..." "Yellow"
        $rawOutput = winget search . --source winget --accept-source-agreements 2>&1 | Out-String
        $lines = $rawOutput -split "`n"
        
        $allWingetPackages = @()
        $inData = $false
        
        foreach ($line in $lines) {
            if ($line -match '^-{5,}') {
                $inData = $true
                continue
            }
            
            if (-not $inData -or [string]::IsNullOrWhiteSpace($line.Trim())) {
                continue
            }
            
            # Parse right-to-left: last whitespace-separated value is the package ID
            $parts = $line.Trim() -split '\s+' | Where-Object { ![string]::IsNullOrWhiteSpace($_) }
            
            if ($parts.Count -gt 0) {
                $id = $parts[-1]  # Last element
                if ($id -match '\.' -and $id -match '[a-zA-Z]') {
                    $allWingetPackages += $id
                }
            }
        }
    }
} catch {
    Write-Log "Error getting package list: $_" "Red"
    exit 1
}

# Remove duplicates
$allWingetPackages = $allWingetPackages | Select-Object -Unique

Write-Log "Found $($allWingetPackages.Count) packages in winget"

Write-Log ""
Write-Log "Step 2: Getting existing packages from database..." "Cyan"
$existingPackages = & sqlite3\sqlite3.exe $dbPath "SELECT package_id FROM apps;" | Where-Object { $_ }
$existingSet = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($pkg in $existingPackages) {
    [void]$existingSet.Add($pkg)
}

Write-Log "Found $($existingSet.Count) packages in database"

Write-Log ""
Write-Log "Step 3: Finding missing packages..." "Cyan"
$missingPackages = $allWingetPackages | Where-Object { -not $existingSet.Contains($_) }

Write-Log "Found $($missingPackages.Count) missing packages" "Yellow"

if ($missingPackages.Count -eq 0) {
    Write-Log ""
    Write-Log "No missing packages found. Database is up to date!" "Green"
    exit 0
}

Write-Log ""
Write-Log "Missing packages:"
$missingPackages | Select-Object -First 20 | ForEach-Object { Write-Log "  - $_" }
if ($missingPackages.Count -gt 20) {
    Write-Log "  ... and $($missingPackages.Count - 20) more"
}

if ($DryRun) {
    Write-Log ""
    Write-Log "[DRY RUN] Would add $($missingPackages.Count) packages" "Yellow"
    exit 0
}

Write-Host ""
$userInput = Read-Host "Do you want to add these $($missingPackages.Count) missing packages? (Y/N)"
if ($userInput -ne 'Y' -and $userInput -ne 'y') {
    Write-Log "Operation cancelled by user" "Yellow"
    exit 0
}

Write-Log ""
Write-Log "Step 4: Adding missing packages..." "Cyan"
$startTime = Get-Date
$added = 0
$failed = 0
$packageTimes = @()

for ($i = 0; $i -lt $missingPackages.Count; $i++) {
    $packageId = $missingPackages[$i]
    $packageStartTime = Get-Date
    
    # Calculate detailed progress stats
    $elapsed = (Get-Date) - $startTime
    $elapsedFormatted = "{0:D2}:{1:D2}:{2:D2}" -f $elapsed.Hours, $elapsed.Minutes, $elapsed.Seconds
    $remaining = $missingPackages.Count - $i
    $completed = $i
    
    # Calculate ETA and rate
    if ($completed -gt 0) {
        $avgTimePerPackage = $elapsed.TotalSeconds / $completed
        $etaSeconds = $remaining * $avgTimePerPackage
        $etaFormatted = "{0:D2}:{1:D2}:{2:D2}" -f [math]::Floor($etaSeconds / 3600), [math]::Floor(($etaSeconds % 3600) / 60), [math]::Floor($etaSeconds % 60)
        $rate = [math]::Round($completed / $elapsed.TotalSeconds * 60, 2)
        $avgPackageTime = [math]::Round($avgTimePerPackage, 2)
    } else {
        $etaFormatted = "Calculating..."
        $rate = 0
        $avgPackageTime = 0
    }
    
    # Show detailed progress every package
    Write-Host ""
    Write-Host "=" * 80 -ForegroundColor Cyan
    Write-Host "[$($i + 1)/$($missingPackages.Count)] Processing: $packageId" -ForegroundColor Cyan
    Write-Host "Elapsed: $elapsedFormatted | ETA: $etaFormatted | Rate: $rate pkg/min | Avg: ${avgPackageTime}s/pkg" -ForegroundColor Cyan
    Write-Host "Remaining: $remaining packages | Added: $added | Failed: $failed" -ForegroundColor Cyan
    Write-Host "=" * 80 -ForegroundColor Cyan
    Write-Log "[$($i + 1)/$($missingPackages.Count)] Processing: $packageId"
    
    $package = Get-PackageInfo $packageId
    if ($package -and $package.name) {
        if (Add-PackageToDatabase $package) {
            $added++
            $packageEndTime = Get-Date
            $packageDuration = ($packageEndTime - $packageStartTime).TotalSeconds
            $packageTimes += $packageDuration
            Write-Host "  ✓ SUCCESS - Took $([math]::Round($packageDuration, 2))s" -ForegroundColor Green
        } else {
            $failed++
            Write-Host "  ✗ FAILED to add to database" -ForegroundColor Red
        }
    } else {
        Write-Log "  Skipped: Could not retrieve info" "Yellow"
        Write-Host "  ⚠ SKIPPED - Could not retrieve package info" -ForegroundColor Yellow
        $failed++
    }
    
    Start-Sleep -Milliseconds 100
}

$totalTime = (Get-Date) - $startTime
$totalTimeFormatted = "{0:D2}:{1:D2}:{2:D2}" -f $totalTime.Hours, $totalTime.Minutes, $totalTime.Seconds

# Calculate statistics
$avgTime = if ($packageTimes.Count -gt 0) { ($packageTimes | Measure-Object -Average).Average } else { 0 }
$minTime = if ($packageTimes.Count -gt 0) { ($packageTimes | Measure-Object -Minimum).Minimum } else { 0 }
$maxTime = if ($packageTimes.Count -gt 0) { ($packageTimes | Measure-Object -Maximum).Maximum } else { 0 }

Write-Host ""
Write-Host "=" * 80 -ForegroundColor Green
Write-Host "                           RESTORE COMPLETE" -ForegroundColor Green
Write-Host "=" * 80 -ForegroundColor Green
Write-Host "Total missing packages: $($missingPackages.Count)" -ForegroundColor White
Write-Host "Successfully added:     $added" -ForegroundColor Green
Write-Host "Failed:                 $failed" -ForegroundColor Red
Write-Host "Success rate:           $([math]::Round(($added / $missingPackages.Count) * 100, 1))%" -ForegroundColor White
Write-Host ""
Write-Host "Time elapsed:           $totalTimeFormatted" -ForegroundColor White
Write-Host "Average time/package:   $([math]::Round($avgTime, 2))s" -ForegroundColor White
Write-Host "Fastest package:        $([math]::Round($minTime, 2))s" -ForegroundColor White
Write-Host "Slowest package:        $([math]::Round($maxTime, 2))s" -ForegroundColor White
Write-Host "Overall rate:           $([math]::Round($added / $totalTime.TotalMinutes, 2)) pkg/min" -ForegroundColor White
Write-Host ""
Write-Host "Database updated: $dbPath" -ForegroundColor Cyan
Write-Host "=" * 80 -ForegroundColor Green

Write-Log "=== RESTORE COMPLETE ==="
Write-Log "Total missing packages: $($missingPackages.Count)"
Write-Log "Successfully added: $added"
Write-Log "Failed: $failed"
Write-Log "Time elapsed: $totalTimeFormatted"

$finalCount = & sqlite3\sqlite3.exe $dbPath "SELECT COUNT(*) FROM apps;"
Write-Log "Final database count: $finalCount apps"
Write-Host ""
Write-Host "Final database count: $finalCount apps" -ForegroundColor Cyan

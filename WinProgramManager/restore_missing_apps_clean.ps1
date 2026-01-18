# Restore Missing Apps to Database
# This script compares winget packages with database entries and adds missing apps

param(
    [int]$BatchSize = 100,
    [switch]$DryRun,
    [switch]$QuickMode
)

$ErrorActionPreference = "Continue"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$PSDefaultParameterValues['*:Encoding'] = 'utf8'

$scriptDir = $PSScriptRoot
$dbPath = Join-Path $scriptDir "WinProgramManager.db"
$ignoreDbPath = Join-Path $scriptDir "WinProgramManagerIgnore.db"
$logPath = Join-Path $scriptDir "restore_missing_apps.log"
$sqlite3Path = Join-Path $scriptDir "sqlite3\sqlite3.exe"

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
    
    $result = & $sqlite3Path $ignoreDbPath "SELECT COUNT(*) FROM ignored_tags WHERE tag_name = '$($Tag.Replace("'", "''"))' COLLATE NOCASE;"
    return [int]$result -gt 0
}

# Get package info from winget
function Get-PackageInfo {
    param([string]$PackageId)
    
    try {
        Write-Log "Querying winget for: $PackageId" "Cyan"
        $info = winget show $PackageId --accept-source-agreements 2>$null | Out-String
        
        if ([string]::IsNullOrWhiteSpace($info)) {
            Write-Log "Winget returned empty response for: $PackageId" "Yellow"
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
    $categoryId = & $sqlite3Path $dbPath $existingSql
    
    if ([string]::IsNullOrWhiteSpace($categoryId)) {
        $insertSql = "INSERT INTO categories (category_name) VALUES ('$($CategoryName.Replace("'", "''"))'); SELECT last_insert_rowid();"
        $categoryId = & $sqlite3Path $dbPath $insertSql
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
        # Escape SQL strings - handle null values
        $pkgId = if ($Package.package_id) { $Package.package_id.Replace("'", "''") } else { "" }
        $name = if ($Package.name) { $Package.name.Replace("'", "''") } else { "" }
        $version = if ($Package.version) { $Package.version.Replace("'", "''") } else { "" }
        $publisher = if ($Package.publisher) { $Package.publisher.Replace("'", "''") } else { "" }
        $description = if ($Package.description) { $Package.description.Replace("'", "''") } else { "" }
        
        $insertSql = "INSERT INTO apps (package_id, name, version, publisher, description, processed_at) VALUES ('$pkgId', '$name', '$version', '$publisher', '$description', datetime('now')); SELECT last_insert_rowid();"
        
        $appId = & $sqlite3Path $dbPath $insertSql
        
        if ([string]::IsNullOrWhiteSpace($appId)) {
            Write-Log "  Failed to insert app $($Package.package_id)" "Red"
            return $false
        }
        
        # Add categories from tags
        foreach ($tag in $Package.tags) {
            $categoryId = Get-CategoryId $tag
            $linkSql = "INSERT OR IGNORE INTO app_categories (app_id, category_id) VALUES ($appId, $categoryId);"
            & $sqlite3Path $dbPath $linkSql | Out-Null
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

$tempFile = [System.IO.Path]::GetTempFileName() + ".json"
Write-Log "Getting package list from winget source..."

try {
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
            
            # Split on 2+ spaces to separate columns: Name | ID | Version | Source
            # Name can have single spaces, but columns are separated by 2+ spaces
            $parts = $line -split '\s{2,}' | Where-Object { ![string]::IsNullOrWhiteSpace($_) }
            
            if ($parts.Count -ge 2) {
                # The ID column might have ID and Version separated by single space
                # So take only the first word from the second part
                $idPart = $parts[1].Trim() -split '\s+' | Select-Object -First 1
                # Validate it's a proper package ID (must match winget ID format)
                if ($idPart -match '^[A-Za-z0-9][\w._-]*\.[\w._-]+$') {
                    $allWingetPackages += $idPart
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

$existingPackages = & $sqlite3Path $dbPath "SELECT package_id FROM apps;" | Where-Object { $_ }
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
        
        # Ensure ETA values are valid numbers
        if ($etaSeconds -lt 0 -or $etaSeconds -gt 86400 -or [double]::IsNaN($etaSeconds) -or [double]::IsInfinity($etaSeconds)) {
            $etaFormatted = "Calculating..."
        } else {
            $etaHours = [int][math]::Floor([math]::Abs($etaSeconds) / 3600)
            $etaMinutes = [int][math]::Floor(([math]::Abs($etaSeconds) % 3600) / 60)
            $etaSecondsRem = [int][math]::Floor([math]::Abs($etaSeconds) % 60)
            $etaFormatted = "{0:D2}:{1:D2}:{2:D2}" -f $etaHours, $etaMinutes, $etaSecondsRem
        }
        
        $rate = [math]::Round($completed / $elapsed.TotalSeconds * 60, 2)
        $avgPackageTime = [math]::Round($avgTimePerPackage, 2)
    } else {
        $etaFormatted = "Calculating..."
        $rate = 0
        $avgPackageTime = 0
    }
    
    # Show detailed progress every package
    Write-Host ""
    Write-Host ("=" * 80) -ForegroundColor Cyan
    Write-Host "[$($i + 1)/$($missingPackages.Count)] Processing: $packageId" -ForegroundColor Cyan
    Write-Host "Elapsed: $elapsedFormatted | ETA: $etaFormatted | Rate: $rate pkg/min | Avg: ${avgPackageTime}s/pkg" -ForegroundColor Cyan
    Write-Host "Remaining: $remaining packages | Added: $added | Failed: $failed" -ForegroundColor Cyan
    Write-Host ("=" * 80) -ForegroundColor Cyan
    Write-Log "[$($i + 1)/$($missingPackages.Count)] Processing: $packageId"
    
    $package = Get-PackageInfo $packageId
    if ($package -and $package.name) {
        if (Add-PackageToDatabase $package) {
            $added++
            $packageEndTime = Get-Date
            $packageDuration = ($packageEndTime - $packageStartTime).TotalSeconds
            $packageTimes += $packageDuration
            $successTime = [math]::Round($packageDuration, 2)
            Write-Host "  SUCCESS - Took ${successTime}s" -ForegroundColor Green
        } else {
            $failed++
            Write-Host "  FAILED to add to database" -ForegroundColor Red
        }
    } else {
        Write-Log "  Skipped: Could not retrieve info" "Yellow"
        Write-Host "  SKIPPED - Could not retrieve package info" -ForegroundColor Yellow
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
Write-Host ("=" * 80) -ForegroundColor Green
Write-Host "                           RESTORE COMPLETE" -ForegroundColor Green
Write-Host ("=" * 80) -ForegroundColor Green
Write-Host "Total missing packages: $($missingPackages.Count)" -ForegroundColor White
Write-Host "Successfully added:     $added" -ForegroundColor Green
Write-Host "Failed:                 $failed" -ForegroundColor Red

$successRate = if ($missingPackages.Count -gt 0) { [math]::Round(($added / $missingPackages.Count) * 100, 1) } else { 0 }
Write-Host "Success rate:           $successRate%" -ForegroundColor White
Write-Host ""
Write-Host "Time elapsed:           $totalTimeFormatted" -ForegroundColor White

$avgRounded = [math]::Round($avgTime, 2)
$minRounded = [math]::Round($minTime, 2)
$maxRounded = [math]::Round($maxTime, 2)
Write-Host "Average time/package:   ${avgRounded}s" -ForegroundColor White
Write-Host "Fastest package:        ${minRounded}s" -ForegroundColor White
Write-Host "Slowest package:        ${maxRounded}s" -ForegroundColor White

$overallRate = if ($totalTime.TotalMinutes -gt 0) { [math]::Round($added / $totalTime.TotalMinutes, 2) } else { 0 }
Write-Host "Overall rate:           $overallRate pkg/min" -ForegroundColor White
Write-Host ""
Write-Host "Database updated: $dbPath" -ForegroundColor Cyan
Write-Host ("=" * 80) -ForegroundColor Green

Write-Log "=== RESTORE COMPLETE ==="
Write-Log "Total missing packages: $($missingPackages.Count)"
Write-Log "Successfully added: $added"
Write-Log "Failed: $failed"
Write-Log "Time elapsed: $totalTimeFormatted"

$finalCount = & $sqlite3Path $dbPath "SELECT COUNT(*) FROM apps;"
Write-Log "Final database count: $finalCount apps"
Write-Host ""
Write-Host "Final database count: $finalCount apps" -ForegroundColor Cyan

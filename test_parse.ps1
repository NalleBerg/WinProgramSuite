# Test parsing of winget upgrade output
# Parse from right to left: Source, Available, Version, ID, Name (rest)

$output = winget upgrade | Out-String

Write-Host "=== RAW OUTPUT ===" -ForegroundColor Cyan
Write-Host $output
Write-Host ""

Write-Host "=== PARSING LINE BY LINE ===" -ForegroundColor Cyan

$lines = $output -split "`n"
$pastHeader = $false

foreach ($line in $lines) {
    $line = $line.TrimEnd("`r")
    
    # Skip until separator
    if (-not $pastHeader) {
        if ($line -match "----") {
            $pastHeader = $true
            Write-Host "Found header separator" -ForegroundColor Green
        }
        continue
    }
    
    # Stop at footer
    if ($line -match "upgrades available") {
        Write-Host "Found footer" -ForegroundColor Green
        break
    }
    
    # Skip empty lines
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    
    # Split by whitespace
    $tokens = $line -split '\s+' | Where-Object { $_ -ne '' }
    
    Write-Host "`nLine: '$line'" -ForegroundColor Yellow
    Write-Host "Tokens count: $($tokens.Count)" -ForegroundColor Magenta
    
    if ($tokens.Count -lt 5) {
        Write-Host "  SKIP: Not enough tokens (need 5+)" -ForegroundColor Red
        continue
    }
    
    $n = $tokens.Count
    
    # Parse from right to left
    $source = $tokens[$n-1]
    $available = $tokens[$n-2]
    $version = $tokens[$n-3]
    $id = $tokens[$n-4]
    
    # Name is everything before ID
    $nameParts = @()
    for ($i = 0; $i -lt ($n - 4); $i++) {
        $nameParts += $tokens[$i]
    }
    $name = $nameParts -join ' '
    
    if ($name -eq '') { $name = $id }
    
    Write-Host "  Source   : '$source'" -ForegroundColor White
    Write-Host "  Available: '$available'" -ForegroundColor White
    Write-Host "  Version  : '$version'" -ForegroundColor White
    Write-Host "  ID       : '$id'" -ForegroundColor Green
    Write-Host "  Name     : '$name'" -ForegroundColor Cyan
    
    # Check if ID contains spaces (invalid)
    if ($id -match '\s') {
        Write-Host "  WARNING: ID contains spaces!" -ForegroundColor Red
    }
}

Write-Host "`n=== END ===" -ForegroundColor Cyan

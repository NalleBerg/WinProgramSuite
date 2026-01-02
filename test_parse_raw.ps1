# Test parsing the actual wup_winget_raw.txt file
$content = Get-Content -Path 'wup_winget_raw.txt' -Raw

Write-Host "=== CONTENT ===" -ForegroundColor Cyan
Write-Host $content
Write-Host ""

Write-Host "=== PARSING ===" -ForegroundColor Cyan
$lines = $content -split "`n"
$pastHeader = $false

foreach ($line in $lines) {
    $line = $line.TrimEnd("`r")
    
    # Skip until separator
    if (-not $pastHeader) {
        if ($line -match "----") {
            $pastHeader = $true
            Write-Host "Found separator" -ForegroundColor Green
        }
        continue
    }
    
    # Stop at footer
    if ($line -match "upgrades available") {
        Write-Host "Found footer, stopping" -ForegroundColor Green
        break
    }
    
    # Skip empty lines
    if ([string]::IsNullOrWhiteSpace($line)) { 
        Write-Host "Skipping empty line" -ForegroundColor Gray
        continue 
    }
    
    # Split by whitespace
    $tokens = $line -split '\s+' | Where-Object { $_ -ne '' }
    
    Write-Host "`nLine: '$line'" -ForegroundColor Yellow
    Write-Host "Token count: $($tokens.Count)" -ForegroundColor Magenta
    
    if ($tokens.Count -lt 5) {
        Write-Host "  SKIP: Not enough tokens (need 5+)" -ForegroundColor Red
        continue
    }
    
    $n = $tokens.Count
    $source = $tokens[$n-1]
    $available = $tokens[$n-2]
    $version = $tokens[$n-3]
    $id = $tokens[$n-4]
    
    $nameParts = @()
    for ($i = 0; $i -lt ($n - 4); $i++) {
        $nameParts += $tokens[$i]
    }
    $name = $nameParts -join ' '
    if ($name -eq '') { $name = $id }
    
    Write-Host "  ID: '$id'" -ForegroundColor Green
    Write-Host "  Name: '$name'" -ForegroundColor Cyan
    Write-Host "  Version: '$version'" -ForegroundColor White
    Write-Host "  Available: '$available'" -ForegroundColor White
    Write-Host "  Source: '$source'" -ForegroundColor White
}

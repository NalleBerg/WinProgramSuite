# Robust winget upgrade parser
# Writes raw capture to wup_winget_raw_fallback.txt and parsed CSV to wup_winget_parsed.csv

$raw = & winget upgrade --accept-source-agreements --accept-package-agreements 2>&1
# Save raw capture
$raw | Out-File -Encoding utf8 wup_winget_raw_fallback.txt

Param(
    [switch]$Interactive
)

# Robust winget upgrade/list parser
# Writes raw captures to wup_winget_raw_fallback.txt, list fallback to wup_winget_list_fallback.txt
# and parsed CSV to wup_winget_parsed.csv. Shows a dialog when no applicable updates found only when
# called with -Interactive.

Add-Type -AssemblyName System.Windows.Forms | Out-Null

$raw = & winget upgrade 2>&1
# Save raw capture
$raw | Out-File -Encoding utf8 wup_winget_raw_fallback.txt

$allText = $raw -join "`n"

$resultLines = @()

# Detect the generic 'not applicable' notice
$upgradeNotice = $false
if ($allText -like '*A newer package version is available in a configured source, but it does not apply to your system or requirements.*') {
    $upgradeNotice = $true
}

# Try JSON tail (some winget versions may append JSON)
try {
    $start = $allText.IndexOf('[')
    if ($start -ge 0) {
        $jsonText = $allText.Substring($start)
        $jsonObj = $jsonText | ConvertFrom-Json -ErrorAction Stop
        foreach ($item in $jsonObj) {
            try {
                if ([version]$item.Available -gt [version]$item.Version) {
                    $resultLines += "$($item.Name),$($item.Id),$($item.Version),$($item.Available)"
                }
            } catch {}
        }
    }
} catch {}

# Always run `winget list` to get installed vs available info (robust fallback)
try {
    $listRaw = & winget list --source winget 2>&1
    $listRaw | Out-File -Encoding utf8 wup_winget_list_fallback.txt
    foreach ($line in $listRaw) {
        $t = $line.Trim()
        if ($t -eq '') { continue }
        if ($t -match '^Name\s+Id\s+Version') { continue }
        if ($t -match '^[-]+') { continue }

        # Try to extract Id (contains a dot) and versions
        $idMatch = [regex]::Match($t, '([A-Za-z0-9_.-]+\.[A-Za-z0-9_.-]+)')
        if ($idMatch.Success) {
            $id = $idMatch.Value
            # text after id
            $after = $t.Substring($t.IndexOf($id) + $id.Length).Trim()
            # find version tokens in the remainder
            $verMatches = [regex]::Matches($after, '\\d+(?:\\.[0-9A-Za-z-]+)*')
            if ($verMatches.Count -ge 1) {
                $installed = $verMatches[0].Value
                $available = $null
                if ($verMatches.Count -ge 2) { $available = $verMatches[1].Value }
                if ($available) {
                    try {
                        if ([version]$available -gt [version]$installed) {
                            $name = $t.Substring(0, $t.IndexOf($id)).Trim()
                            if ($upgradeNotice) { $note = 'NotApplicable' } else { $note = '' }
                            if ($note -ne '') { $out = "$name,$id,$installed,$available,$note" } else { $out = "$name,$id,$installed,$available" }
                            if (-not ($resultLines -contains $out)) { $resultLines += $out }
                        }
                    } catch {}
                }
            }
        } else {
            # fallback: detect lines with two version tokens anywhere
            $vm = [regex]::Matches($t, '\\d+(?:\\.[0-9A-Za-z-]+)*')
            if ($vm.Count -ge 2) {
                $installed = $vm[$vm.Count-2].Value
                $available = $vm[$vm.Count-1].Value
                $name = $t
                try {
                    if ([version]$available -gt [version]$installed) {
                        $out = "$name,,$installed,$available"
                        if (-not ($resultLines -contains $out)) { $resultLines += $out }
                    }
                } catch {}
            }
        }
    }
} catch {}

# Write parsed CSV
$csvPath = 'wup_winget_parsed.csv'
$resultLines | Out-File -Encoding utf8 -FilePath $csvPath

if ($resultLines.Count -eq 0) {
    if ($Interactive) { [System.Windows.Forms.MessageBox]::Show('Your system is updated!','WinUpdate') | Out-Null }
    Write-Output ""
    exit 0
} else {
    $resultLines | ForEach-Object { Write-Output $_ }
    exit 0
}

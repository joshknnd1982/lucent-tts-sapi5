# Fails the build if any shipped binary went out without identity metadata.
#
# The 1.0.0 release shipped LucentSAPI.dll with a completely empty version resource and a
# Setup.exe with a blank FileVersion.  An unsigned, metadata-free DLL that registers
# itself as an in-process COM server is a shape Defender's ML models score badly, so this
# check exists to stop that combination from ever shipping again by accident.
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$OutputDir)

$ErrorActionPreference = 'Stop'

$targets = @(
    (Join-Path $OutputDir 'LucentSAPI.dll'),
    (Join-Path $OutputDir 'x64\LucentSAPI.dll'),
    (Join-Path $OutputDir 'LucentConfig.exe'),
    (Join-Path $OutputDir 'LucentSAPI_Setup.exe')
)

$bad = @()
foreach ($t in $targets) {
    if (-not (Test-Path $t)) { Write-Host "MISSING  $t"; $bad += $t; continue }

    $v = (Get-Item $t).VersionInfo
    $sig = (Get-AuthenticodeSignature $t).Status

    # Inno pads its version strings with trailing spaces; trim before testing.
    $fileVer = "$($v.FileVersion)".Trim()
    $company = "$($v.CompanyName)".Trim()
    $product = "$($v.ProductName)".Trim()
    $desc    = "$($v.FileDescription)".Trim()

    Write-Host ("{0,-24} ver={1,-10} company='{2}' desc='{3}' signature={4}" -f `
        (Split-Path $t -Leaf), $fileVer, $company, $desc, $sig)

    if (-not $fileVer -or -not $company -or -not $product -or -not $desc) { $bad += $t }
}

if ($bad.Count -gt 0) {
    Write-Host ''
    Write-Host 'ERROR: the following files have incomplete version metadata:'
    $bad | ForEach-Object { Write-Host "  $_" }
    exit 1
}

Write-Host ''
Write-Host 'All shipped binaries carry complete version metadata.'
if ((Get-AuthenticodeSignature (Join-Path $OutputDir 'LucentSAPI_Setup.exe')).Status -eq 'NotSigned') {
    Write-Host 'NOTE: this build is UNSIGNED. SmartScreen ("Windows protected your PC") will'
    Write-Host '      appear for anyone who downloads it until the installer is signed with a'
    Write-Host '      certificate from a publicly trusted CA. See README.md, "Code signing".'
}
exit 0

# Fails the build if any shipped binary went out without identity metadata, or if the
# version it carries is not the one src\version.h and installer\lucent.iss agree on.
#
# The 1.0.0 release shipped LucentSAPI.dll with a completely empty version resource and a
# Setup.exe with a blank FileVersion.  An unsigned, metadata-free DLL that registers
# itself as an in-process COM server is a shape Defender's ML models score badly, so this
# check exists to stop that combination from ever shipping again by accident.
#
# The setup file name carries the version too (LucentSAPI_Setup_1.1.0.exe), so two
# downloads in one folder are distinguishable before either is opened, and a release
# asset is never silently replaced by a different build with the same name.
[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$OutputDir)

$ErrorActionPreference = 'Stop'

$repo = Split-Path -Parent $PSScriptRoot

# The two places the version is written by hand must agree, or the shipped file name and
# the resource inside it would disagree.
$issText = Get-Content (Join-Path $repo 'installer\lucent.iss') -Raw
if ($issText -notmatch '(?m)^\s*#define\s+MyAppVersion\s+"([0-9.]+)"') {
    Write-Host 'ERROR: cannot find MyAppVersion in installer\lucent.iss'
    exit 1
}
$issVersion = $Matches[1]

$hdrText = Get-Content (Join-Path $repo 'src\version.h') -Raw
if ($hdrText -notmatch '(?m)^\s*#define\s+LUCENT_VERSION_STR\s+"([0-9.]+)"') {
    Write-Host 'ERROR: cannot find LUCENT_VERSION_STR in src\version.h'
    exit 1
}
$hdrVersion = $Matches[1]

if ("$issVersion.0" -ne $hdrVersion) {
    Write-Host "ERROR: version mismatch - lucent.iss says $issVersion, version.h says $hdrVersion"
    exit 1
}

$setupName = "LucentSAPI_Setup_$issVersion.exe"
Write-Host "Expected version $hdrVersion, installer $setupName"

$targets = @(
    (Join-Path $OutputDir 'LucentSAPI.dll'),
    (Join-Path $OutputDir 'x64\LucentSAPI.dll'),
    (Join-Path $OutputDir 'LucentConfig.exe'),
    (Join-Path $OutputDir $setupName)
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

    Write-Host ("{0,-28} ver={1,-10} company='{2}' desc='{3}' signature={4}" -f `
        (Split-Path $t -Leaf), $fileVer, $company, $desc, $sig)

    if (-not $fileVer -or -not $company -or -not $product -or -not $desc) { $bad += $t; continue }
    if ($fileVer -ne $hdrVersion) {
        Write-Host "         ^ expected $hdrVersion"
        $bad += $t
    }
}

if ($bad.Count -gt 0) {
    Write-Host ''
    Write-Host 'ERROR: the following files have missing or wrong version metadata:'
    $bad | ForEach-Object { Write-Host "  $_" }
    exit 1
}

Write-Host ''
Write-Host 'All shipped binaries carry complete version metadata.'
if ((Get-AuthenticodeSignature (Join-Path $OutputDir $setupName)).Status -eq 'NotSigned') {
    Write-Host 'NOTE: this build is UNSIGNED. SmartScreen ("Windows protected your PC") will'
    Write-Host '      appear for anyone who downloads it until the installer is signed with a'
    Write-Host '      certificate from a publicly trusted CA. See README.md, "Code signing".'
}
exit 0

# Authenticode-signs the shipped binaries, when a code signing certificate is configured.
#
# Why this exists: an unsigned download is what makes Windows show "Windows protected your
# PC" (SmartScreen) and is the single largest input to Microsoft Defender's reputation
# score.  Metadata alone reduces the odds of a false positive; only a certificate from a
# publicly trusted CA removes the warning outright.
#
# Configure exactly one of the following before building, then run build_all.bat:
#
#   $env:LUCENT_SIGN_THUMBPRINT = '<40 hex chars>'   # certificate already in the store
#   $env:LUCENT_SIGN_PFX        = 'C:\path\cert.pfx' # .pfx file on disk
#   $env:LUCENT_SIGN_PASS       = '<pfx password>'   # optional, with LUCENT_SIGN_PFX
#
# Optional:
#   $env:LUCENT_SIGN_TS = 'http://timestamp.digicert.com'   # RFC3161 timestamp server
#
# With nothing configured the script prints what is missing and exits 0, so an unsigned
# build still succeeds.
#
# Usage:  powershell -ExecutionPolicy Bypass -File installer\sign.ps1 <file> [<file> ...]
[CmdletBinding()]
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Files)

$ErrorActionPreference = 'Stop'

function Find-SignTool {
    $candidates = @()
    foreach ($base in @("${env:ProgramFiles(x86)}\Windows Kits\10\bin", "$env:ProgramFiles\Windows Kits\10\bin")) {
        if (Test-Path $base) {
            $candidates += Get-ChildItem $base -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
                Where-Object { $_.DirectoryName -match '\\x64$' } |
                Sort-Object FullName -Descending
        }
    }
    if ($candidates.Count -eq 0) { return $null }
    return $candidates[0].FullName
}

if (-not $Files -or $Files.Count -eq 0) { Write-Host 'sign.ps1: no files given, nothing to do.'; exit 0 }

$thumb = $env:LUCENT_SIGN_THUMBPRINT
$pfx   = $env:LUCENT_SIGN_PFX
if (-not $thumb -and -not $pfx) {
    Write-Host 'sign.ps1: no code signing certificate configured - shipping UNSIGNED.'
    Write-Host '          Set LUCENT_SIGN_THUMBPRINT or LUCENT_SIGN_PFX to sign.'
    Write-Host '          Unsigned downloads always trigger the SmartScreen warning.'
    exit 0
}

$signtool = Find-SignTool
if (-not $signtool) { throw 'sign.ps1: signtool.exe not found - install the Windows 10/11 SDK signing tools.' }
Write-Host "sign.ps1: using $signtool"

$ts = $env:LUCENT_SIGN_TS
if (-not $ts) { $ts = 'http://timestamp.digicert.com' }

# SHA-256 throughout: SHA-1 Authenticode signatures are no longer trusted by Windows.
$common = @('/fd', 'sha256', '/td', 'sha256', '/tr', $ts, '/v')
if ($thumb) {
    $auth = @('/sha1', $thumb)
} else {
    if (-not (Test-Path $pfx)) { throw "sign.ps1: LUCENT_SIGN_PFX not found: $pfx" }
    $auth = @('/f', $pfx)
    if ($env:LUCENT_SIGN_PASS) { $auth += @('/p', $env:LUCENT_SIGN_PASS) }
}

foreach ($f in $Files) {
    if (-not (Test-Path $f)) { throw "sign.ps1: missing file $f" }
    Write-Host "sign.ps1: signing $f"
    & $signtool sign @auth @common $f
    if ($LASTEXITCODE -ne 0) { throw "sign.ps1: signtool failed on $f (exit $LASTEXITCODE)" }
}

foreach ($f in $Files) {
    $s = Get-AuthenticodeSignature $f
    Write-Host ("sign.ps1: {0} -> {1}" -f (Split-Path $f -Leaf), $s.Status)
}
Write-Host 'sign.ps1: done.'

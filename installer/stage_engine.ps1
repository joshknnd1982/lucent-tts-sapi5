# Stages the Lucent engine into bin\engine in the layout the wrapper and installer expect:
#   bin\engine\ttsserver.exe          the engine (from Lucent_3x2_TTS_bin)
#   bin\engine\x<lang>.inton          intonation parameter copies (must sit next to the exe)
#   bin\engine\data\languages\...     per-language data + common
#   bin\engine\data\chfiles\...       channel-file templates
# Run from the repository root:  powershell -ExecutionPolicy Bypass -File installer\stage_engine.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src = Join-Path $root 'bin'
$dst = Join-Path $src 'engine'
$exe = Join-Path $src 'Lucent_3x2_TTS_bin\ttsserver.exe'
$data = Join-Path $src 'Lucent_3x2_TTS_data'
if (-not (Test-Path $exe)) { throw "missing $exe" }
if (-not (Test-Path $data)) { throw "missing $data" }
New-Item -ItemType Directory -Force (Join-Path $dst 'data') | Out-Null
Copy-Item $exe $dst -Force
Copy-Item (Join-Path $data 'languages') (Join-Path $dst 'data') -Recurse -Force
Copy-Item (Join-Path $data 'chfiles') (Join-Path $dst 'data') -Recurse -Force
Get-ChildItem (Join-Path $dst 'data\languages') -Directory | ForEach-Object {
    Get-ChildItem $_.FullName -Filter 'x*.inton' | ForEach-Object { Copy-Item $_.FullName $dst -Force }
}
# a couple of leftovers from the original installer are not needed at run time
Remove-Item (Join-Path $dst 'data\languages\*\*.reg') -Force -ErrorAction SilentlyContinue
Write-Host "staged engine into $dst"

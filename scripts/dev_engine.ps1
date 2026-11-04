# Fast engine iteration: builds libcef and bootstrap in the non-official
# component output (out\Dev_x64; is_official_build=false, symbol_level=0,
# enable_cdm_host_verification=true). An incremental build takes 0.5-3 minutes
# (the official out\Release_GN_x64 takes about 10-25). Edit CEF/Chromium in the
# checkout, run this, then scripts\dev_red.ps1. Not for releases.
param([ValidatePattern('^[A-Za-z]:\\[^ ]+$')][string]$WorkDirectory = 'E:\cef-build')
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = '0'
$env:DEPOT_TOOLS_UPDATE = '0'
$env:PATH = "$WorkDirectory\depot_tools;$env:PATH"
Set-Location "$WorkDirectory\code\chromium\src"
$ErrorActionPreference = 'Continue'
$start = Get-Date
# A changed CEF header needs the C API and wrappers regenerated first.
Push-Location cef
python tools\version_manager.py -u 2>&1 | Select-String -Pattern 'error|mismatch' | Select-Object -First 5
$apiExitCode = $LASTEXITCODE
Pop-Location
if ($apiExitCode) { throw "CEF API generation failed (exit $apiExitCode)" }
& autoninja -C out\Dev_x64 libcef bootstrap 2>&1 | ForEach-Object { "$_" } | Select-String -Pattern 'error|FAILED|finished|stopped' | Select-Object -First 40
$engineExitCode = $LASTEXITCODE
"exit=$engineExitCode minutes=$([math]::Round(((Get-Date) - $start).TotalMinutes, 1))"
if ($engineExitCode) { exit $engineExitCode }

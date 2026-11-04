# Experimental dev build only. Restores the exact touched engine files and GN args;
# the candidate's matching DLLs remain staged separately in build/devplatform.
param([string]$Receipt = '')
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (!$Receipt) { $Receipt = Join-Path $repo ('artifacts/platform-codecs-engine-' + (Get-Date -Format 'yyyyMMdd-HHmmss')) }
$src = 'E:\cef-build\code\chromium\src'
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = '0'
$env:DEPOT_TOOLS_UPDATE = '0'
$env:PATH = 'E:\cef-build\depot_tools;' + $env:PATH
# The full CPython on PATH (depot_tools, prepended above for the build, ships its own).
$python = (Get-Command python.exe -CommandType Application | Where-Object { $_.Source -notlike '*depot_tools*' } | Select-Object -First 1).Source
& $python "$PSScriptRoot\platform_codecs_candidate.py" apply --receipt $Receipt
if ($LASTEXITCODE) { throw 'Candidate preparation failed' }
try {
    Push-Location $src
    try {
        # Only implementation bodies change: CEF API and component ABI stay at
        # the source pin. Build the affected DLLs, retaining the existing matching
        # libcef/bootstrap. A complete release build is a separate promotion gate.
        & autoninja -C out\Dev_x64 ffmpeg.dll media.dll media_mojo_services.dll *> (Join-Path $Receipt 'engine-build.log')
        if ($LASTEXITCODE) { throw "Engine build failed; see $Receipt\engine-build.log" }
    } finally { Pop-Location }
    & "$PSScriptRoot\dev_red.ps1" -ClientOnly -BuildDirectory devplatform *> (Join-Path $Receipt 'client-build.log')
    if ($LASTEXITCODE) { throw "Client build failed; see $Receipt\client-build.log" }
    Copy-Item "$src\out\Dev_x64\args.gn" (Join-Path $Receipt 'built-args.gn')
    Get-FileHash "$repo\build\devplatform\apps\desktop\Release\ffmpeg.dll","$repo\build\devplatform\apps\desktop\Release\libcef.dll" |
        ConvertTo-Json | Set-Content (Join-Path $Receipt 'binaries.json')
    [pscustomobject]@{receipt=$Receipt; executable="$repo\build\devplatform\apps\desktop\Release\sol.exe"} |
        ConvertTo-Json | Set-Content (Join-Path $repo 'artifacts/platform-codecs-current.json')
    "Candidate staged: $repo\build\devplatform\apps\desktop\Release\sol.exe"
} finally {
    & $python "$PSScriptRoot\platform_codecs_candidate.py" restore --receipt $Receipt
    if ($LASTEXITCODE) { throw 'Engine source restoration failed; preserve receipt and inspect concurrent changes' }
}

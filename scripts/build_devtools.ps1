# Client-only development build; retain the previous media pipeline for this task.
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
& "$PSScriptRoot\dev_red.ps1" -ClientOnly -BuildDirectory devtoolsfix
if ($LASTEXITCODE) { throw 'DevTools client build failed' }
$baseline = Join-Path $repo 'build/devtheme/apps/desktop/Release'
$target = Join-Path $repo 'build/devtoolsfix/apps/desktop/Release'
if ((Get-FileHash "$baseline/libcef.dll").Hash -ne (Get-FileHash "$target/libcef.dll").Hash) { throw 'Baseline engine ABI identity differs' }
foreach ($name in 'ffmpeg.dll','media.dll','media_mojo_services.dll') { Copy-Item -LiteralPath (Join-Path $baseline $name) -Destination (Join-Path $target $name) -Force }
'DevTools candidate ready, previous media pipeline retained'

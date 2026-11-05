# Packs the staged release (artifacts/installer/stage-release, made by
# build_installer.ps1) with Velopack into the update feed Sol reads from GitHub
# Releases (app/updates.cpp), and lists the files a release must carry:
#   releases.win.json, SolBrowser-<v>-full.nupkg, SolBrowser-<v>-delta.nupkg
#   (when an earlier Velopack release exists), Sol-Setup-<v>.exe, and the small
#   Sol-Web-Installer.exe (apps/installer) that downloads and runs the latest setup.
# Writes them to artifacts/velopack/publish. See docs/sol-plan.md, stage 4.
param([ValidatePattern('^[A-Za-z0-9_-]+$')][string]$BuildDirectory = 'Release', [switch]$NoDelta)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$version = (Get-Content -LiteralPath (Join-Path $repoRoot 'VERSION') -Raw).Trim()
if ($version -notmatch '^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$') { throw 'Invalid VERSION file' }
$stage = Join-Path $repoRoot 'artifacts/installer/stage-release'
$exe = Join-Path $stage 'sol.exe'
if (!(Test-Path -LiteralPath $exe)) { throw 'Run build_installer.ps1 first: no staged release' }
if ((Get-Item -LiteralPath $exe).VersionInfo.ProductVersion -ne $version) { throw 'Staged sol.exe does not match VERSION' }

# vpk is the Velopack packer, pinned to the same version as velopack_libc.
$lock = Get-Content -LiteralPath (Join-Path $repoRoot 'cmake/velopack.lock.json') -Raw | ConvertFrom-Json
$tools = Join-Path $repoRoot '.cache/vpk'
$vpk = Join-Path $tools 'vpk.exe'
$installed = if (Test-Path -LiteralPath $vpk) { (& dotnet tool list --tool-path $tools | Select-String '^vpk\s+(\S+)').Matches.Groups[1].Value }
if ($installed -ne $lock.version) {
    if ($installed) { & dotnet tool uninstall vpk --tool-path $tools | Out-Null }
    & dotnet tool install vpk --version $lock.version --tool-path $tools
    if ($LASTEXITCODE) { throw 'Installing vpk failed' }
}

$feed = Join-Path $repoRoot 'artifacts/velopack/feed'
$publish = Join-Path $repoRoot 'artifacts/velopack/publish'
foreach ($folder in $feed, $publish) { if (Test-Path -LiteralPath $folder) { Remove-Item -LiteralPath $folder -Recurse -Force } }
New-Item -ItemType Directory -Force -Path $feed, $publish | Out-Null

# The latest published release's full package lets vpk make a delta against it.
# Before the first Velopack release there is none; the update is then full only.
if (!$NoDelta) {
    $ErrorActionPreference = 'Continue'
    # CI passes its token (GH_TOKEN) to avoid the anonymous API rate limit.
    $auth = if ($env:GH_TOKEN) { @('--token', $env:GH_TOKEN) } else { @() }
    & $vpk download github --repoUrl https://github.com/sol-browser/sol --channel win --outputDir $feed @auth 2>&1 | ForEach-Object { "$_" }
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE) { Write-Warning 'No earlier Velopack release; packing without a delta' }
}

# Sol creates its own Start menu shortcut and registration (browser_identity.h,
# default_browser.cpp), so Velopack makes none. The profile lives outside the
# install folder and survives uninstall.
& $vpk pack --packId SolBrowser --packVersion $version --packDir $stage --mainExe sol.exe `
    --packTitle Sol --packAuthors 'Sol Browser' --icon (Join-Path $repoRoot 'apps/desktop/resources/red.ico') `
    --shortcuts None --channel win --runtime win-x64 --outputDir $feed
if ($LASTEXITCODE) { throw 'vpk pack failed' }

$full = Join-Path $feed "SolBrowser-$version-full.nupkg"
$setup = Join-Path $feed 'SolBrowser-win-Setup.exe'
foreach ($required in $full, $setup, (Join-Path $feed 'releases.win.json')) {
    if (!(Test-Path -LiteralPath $required)) { throw "vpk did not produce $required" }
}
Copy-Item -LiteralPath $full, (Join-Path $feed 'releases.win.json') -Destination $publish
$delta = Join-Path $feed "SolBrowser-$version-delta.nupkg"
if (Test-Path -LiteralPath $delta) { Copy-Item -LiteralPath $delta -Destination $publish }
Copy-Item -LiteralPath $setup -Destination (Join-Path $publish "Sol-Setup-$version.exe")
$installer = Join-Path $repoRoot "build/$BuildDirectory/apps/installer/Sol-Web-Installer.exe"
if (!(Test-Path -LiteralPath $installer)) { throw "No built installer: $installer" }
if ((Get-Item -LiteralPath $installer).VersionInfo.ProductVersion -ne $version) { throw 'Sol-Web-Installer.exe does not match VERSION' }
Copy-Item -LiteralPath $installer -Destination $publish
Get-ChildItem -LiteralPath $publish -File | ForEach-Object {
    "$((Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant())  $($_.Name)"
} | Set-Content -LiteralPath (Join-Path $publish "Sol-$version.sha256") -Encoding ascii
Get-ChildItem -LiteralPath $publish -File | ForEach-Object { $_.FullName }

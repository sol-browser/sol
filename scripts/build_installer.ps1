# Packages a finished build into a per-user Windows installer:
#   artifacts/installer/Sol-Setup-<version>.exe
# Stages the build output (without tests), stamps the staged sol.exe
# with Red's icon and version info, then compiles installer/red.iss with the
# checksum-pinned Inno Setup (cmake/innosetup.lock.json). See docs/installer.md.
param([Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$BuildDirectory,
      [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release',
      [ValidatePattern('^\d+\.\d+\.\d+$')][string]$Version,
      # Separate app id, folder (%LOCALAPPDATA%\RedInstallerTest) and profile,
      # for exercising the installer without touching the real install.
      [switch]$TestVariant)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$releaseVersion = (Get-Content -LiteralPath (Join-Path $repoRoot 'VERSION') -Raw).Trim()
if ($releaseVersion -notmatch '^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)$') { throw 'Invalid VERSION file' }
if ($PSBoundParameters.ContainsKey('Version') -and $Version -ne $releaseVersion) { throw 'Change VERSION and rebuild; installer version must match the app' }
$Version = $releaseVersion
python (Join-Path $repoRoot 'scripts/check_release_notes.py')
if ($LASTEXITCODE) { throw 'Release history is invalid or does not match VERSION' }
$source = Join-Path $repoRoot "build/$BuildDirectory/apps/desktop/$Configuration"
foreach ($required in 'sol.exe', 'sol.dll', 'libcef.dll', 'ui/index.html') {
    if (!(Test-Path -LiteralPath (Join-Path $source $required))) { throw "Build is missing $required`: $source" }
}
if ((Get-Item -LiteralPath (Join-Path $source 'sol.dll')).VersionInfo.ProductVersion -ne $Version) {
    throw 'Built DLL version does not match VERSION. Rebuild sol before packaging.'
}
$work = Join-Path $repoRoot 'artifacts/installer'
$variant = if ($TestVariant) { 'test' } else { 'release' }
$stage = Join-Path $work "stage-$variant"
$stage = [IO.Path]::GetFullPath($stage)
if (!$stage.StartsWith([IO.Path]::GetFullPath($work) + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Installer staging must remain inside artifacts/installer'
}
foreach ($folder in @($work, $stage)) {
    if ((Test-Path -LiteralPath $folder) -and ((Get-Item -LiteralPath $folder).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw 'Installer staging cannot use a reparse point'
    }
}
$images = Join-Path $work 'images'
New-Item -ItemType Directory -Force -Path $work | Out-Null

$compiler = python (Join-Path $repoRoot 'scripts/fetch_innosetup.py') | Select-Object -Last 1
if ($LASTEXITCODE -or !(Test-Path -LiteralPath $compiler)) { throw 'Inno Setup setup failed' }

# Mirror the build output, skipping native tests, debug/link files and saved
# manual-probe/previous app DLLs (sol.dll itself is included).
& robocopy $source $stage /MIR /XF *.pdb *.ilk *.lib *.exp *.log *_test.exe sol.*.dll /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "Staging failed (robocopy $LASTEXITCODE)" }
# Build folders retain old hashed UI assets for already-running development
# renderers. Ship only the current dist, whose entry must match this build.
$uiDist = Join-Path $repoRoot 'apps/browser-ui/dist'
$uiEntry = Join-Path $uiDist 'index.html'
if (!(Test-Path -LiteralPath $uiEntry) -or
    (Get-FileHash -LiteralPath $uiEntry).Hash -ne (Get-FileHash -LiteralPath (Join-Path $source 'ui/index.html')).Hash) {
    throw 'Built UI does not match current dist. Rebuild UI and stage sol before packaging.'
}
$stageUi = [IO.Path]::GetFullPath((Join-Path $stage 'ui'))
if (!$stageUi.StartsWith($stage + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
    ((Test-Path -LiteralPath $stageUi) -and ((Get-Item -LiteralPath $stageUi).Attributes -band [IO.FileAttributes]::ReparsePoint))) {
    throw 'Invalid UI staging directory'
}
& robocopy $uiDist $stageUi /MIR /XJ /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "UI staging failed (robocopy $LASTEXITCODE)" }
# Robocopy exclusions preserve already-staged files; remove old runtime logs.
Get-ChildItem -LiteralPath $stage -File -Filter '*.log' | ForEach-Object { Remove-Item -LiteralPath $_.FullName }
# Redistribute CEF's license and Chromium's bundled third-party notices.
$notices = Join-Path $stage 'licenses'
New-Item -ItemType Directory -Force -Path $notices | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot 'third_party/cef/LICENSE.txt') -Destination (Join-Path $notices 'CEF-LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $repoRoot 'third_party/cef/CREDITS.html') -Destination (Join-Path $notices 'Chromium-CREDITS.html')
python (Join-Path $repoRoot 'scripts/brand_exe.py') (Join-Path $stage 'sol.exe') `
    (Join-Path $repoRoot 'apps/desktop/resources/red.ico') $Version
if ($LASTEXITCODE) { throw 'Stamping sol.exe failed' }
if ((Get-Item -LiteralPath (Join-Path $stage 'sol.exe')).VersionInfo.ProductVersion -ne $Version) { throw 'Staged exe version mismatch' }
python (Join-Path $repoRoot 'scripts/make_icons.py') --installer-images $images | Out-Null
if ($LASTEXITCODE) { throw 'Installer images failed (Pillow is required)' }

$defines = @("/DStageDir=$stage", "/DImageDir=$images", "/DAppVersion=$Version", "/DOutputDir=$work")
if ($TestVariant) { $defines += '/DTestVariant' }
# ISCC reports warnings on stderr; judge it by its exit code.
$ErrorActionPreference = 'Continue'
& $compiler /Q @defines (Join-Path $repoRoot 'installer/red.iss') 2>&1 | ForEach-Object { "$_" }
$code = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($code) { throw "Inno Setup compilation failed ($code)" }
$name = if ($TestVariant) { 'SolInstallerTest' } else { 'Sol' }
$setup = Join-Path $work "$name-Setup-$Version.exe"
if (!(Test-Path -LiteralPath $setup)) { throw "Expected installer not found: $setup" }
$files = @(Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName | ForEach-Object {
    @{ path = $_.FullName.Substring($stage.Length).TrimStart([char]92); sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash; size = $_.Length }
})
@{ version = $Version; installer = [IO.Path]::GetFileName($setup); sha256 = (Get-FileHash -LiteralPath $setup -Algorithm SHA256).Hash;
   files = $files } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath ([IO.Path]::ChangeExtension($setup, 'manifest.json')) -Encoding utf8
Write-Output $setup

# Builds the source-locked CEF fork with platform H.264/AAC and no FFmpeg decoders.
# Chromium changes are versioned in the fork's own patch.cfg; no external Red
# patch stack is applied. Needs Visual Studio C++, Windows SDK and depot_tools.
# The archive is copied to .cache. The binary lock is changed only after review.
param([ValidatePattern('^[A-Za-z]:\\[^ ]+$')][string]$WorkDirectory = 'E:\cef-build',
      [switch]$SkipUpdate,
      [switch]$SyncOnly,
      # Validate pins and paths only; no Git checkout, fetch, sync or build.
      [switch]$ValidateOnly,
      # A test build of a candidate branch. Its version name carries the branch
      # (e.g. sol-156-candidate-88), so it is never a release engine.
      [switch]$AllowCandidate,
      [string]$SourceLock = '')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (!$SourceLock) { $SourceLock = Join-Path $repoRoot 'cmake/cef-source.lock.json' }
$sourceHelper = Join-Path $PSScriptRoot 'cef_source.py'
function Read-SourceInfo([string]$Mode) {
    $raw = & python $sourceHelper --lock $SourceLock --work $WorkDirectory --mode $Mode
    if ($LASTEXITCODE) { throw "CEF source validation failed ($Mode)" }
    return ($raw -join "`n") | ConvertFrom-Json
}
function Publish-EngineArchive([string]$SourcePath, [string]$TargetPath) {
    # Never overwrite a cache file in place: fetch_cef.py may be reading it.
    # A same-directory verified temporary file can be published atomically;
    # a reader denying delete sharing makes Replace fail and keeps its bytes.
    $sourceFile = Get-Item -LiteralPath $SourcePath
    $expectedSize = $sourceFile.Length
    $expectedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $SourcePath).Hash.ToLowerInvariant()
    $targetFile = [IO.Path]::GetFullPath($TargetPath)
    $unchanged = [IO.File]::Exists($targetFile) -and
        (Get-Item -LiteralPath $targetFile).Length -eq $expectedSize -and
        (Get-FileHash -Algorithm SHA256 -LiteralPath $targetFile).Hash.ToLowerInvariant() -eq $expectedHash
    if (!$unchanged) {
        $temporary = Join-Path ([IO.Path]::GetDirectoryName($targetFile)) `
            ('.' + [IO.Path]::GetFileName($targetFile) + '.' + [Guid]::NewGuid().ToString('N') + '.partial')
        try {
            [IO.File]::Copy($sourceFile.FullName, $temporary, $false)
            if ((Get-Item -LiteralPath $temporary).Length -ne $expectedSize -or
                (Get-FileHash -Algorithm SHA256 -LiteralPath $temporary).Hash.ToLowerInvariant() -ne $expectedHash) {
                throw 'CEF archive changed while copying; refusing publication'
            }
            if ([IO.File]::Exists($targetFile)) {
                # PS 5.1 binds $null to an empty string for this overload.
                [IO.File]::Replace($temporary, $targetFile, [NullString]::Value)
            } else {
                [IO.File]::Move($temporary, $targetFile)
            }
        } finally {
            # This path is our unique file in the cache, never a directory.
            if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
        }
    }
    $published = Get-Item -LiteralPath $targetFile
    $publishedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $targetFile).Hash.ToLowerInvariant()
    if ($published.Length -ne $expectedSize -or $publishedHash -ne $expectedHash) {
        throw 'Published CEF archive does not match the verified source'
    }
    return [pscustomobject]@{ Name = $published.Name; Length = $published.Length; SHA256 = $publishedHash }
}
if ($SkipUpdate -and $SyncOnly) { throw '-SkipUpdate and -SyncOnly cannot be combined' }
$info = Read-SourceInfo 'config'
if ($ValidateOnly) { $info | ConvertTo-Json -Depth 5; return }
$mode = if ($SkipUpdate) { 'skip' } else { 'update' }
# Check all existing checkouts before modifying the bootstrap tree or .gclient.
$info = Read-SourceInfo $mode
$source = $info.source
$cefSource = $info.paths.bootstrap
$chromiumSource = $info.paths.chromium
$chromiumDir = Split-Path -Parent $chromiumSource
if (!$SkipUpdate) {
    if (!(Test-Path -LiteralPath $cefSource)) {
        git clone --filter=blob:none $source.repository $cefSource
        if ($LASTEXITCODE) { throw 'CEF fork clone failed' }
    }
    git -C $cefSource fetch origin $source.revision
    if ($LASTEXITCODE) { throw 'CEF fork revision fetch failed' }
    git -C $cefSource checkout --detach -q $source.revision
    if ($LASTEXITCODE) { throw 'CEF fork checkout failed' }
}

# CEF names the engine after the checked-out branch (cef_version.py). A release
# engine is built only from the branch in the source lock: merge or push the
# candidate there first. 0.4.0 shipped "sol-156-candidate-88" because the
# official build ran on the candidate branch (docs/cef-fork.md).
function Assert-ReleaseBranch([string]$Checkout) {
    if ($AllowCandidate) { return }
    $branch = (git -C $Checkout rev-parse --abbrev-ref HEAD).Trim()
    if ($branch -ne $source.branch) {
        throw "Release engines are built from '$($source.branch)', not '$branch' ($Checkout). Move the candidate to '$($source.branch)' first, or pass -AllowCandidate for a test build."
    }
}
if ($SkipUpdate) { Assert-ReleaseBranch $info.paths.build_checkout }
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = '0'
$env:CEF_ARCHIVE_FORMAT = 'tar.bz2'
$env:GN_DEFINES = 'is_official_build=true proprietary_codecs=true ffmpeg_branding=Chromium red_platform_codecs=true'
$env:GN_OUT_CONFIGS = 'Release_GN_x64'
if (!$SkipUpdate) {
    # Skip obsolete updater-test installers instead of weakening Defender.
    New-Item -ItemType Directory -Force $chromiumDir | Out-Null
    $skipped = foreach ($product in 'chrome', 'chromium') {
        foreach ($arch in 'win_arm64', 'win_x86', 'win_x86_64') {
            foreach ($suffix in '', '_sans_iid') { "'src/third_party/updater/${product}_$arch$suffix/cipd': None" }
        }
    }
    $chromiumVersion = $source.chromium_tag -replace '^refs/tags/', ''
    $gclient = "solutions = [{'managed': False, 'name': 'src', " +
        "'url': 'https://chromium.googlesource.com/chromium/src.git@$chromiumVersion', " +
        "'custom_vars': {'checkout_pgo_profiles': True, 'source_tarball': False, 'siso_version': 'latest', }, " +
        "'custom_deps': {$($skipped -join ', ')}, 'deps_file': 'DEPS', 'safesync_url': ''}]"
    [IO.File]::WriteAllText((Join-Path $chromiumDir '.gclient'), $gclient)
}

$arguments = @("--download-dir=$WorkDirectory\code", "--depot-tools-dir=$WorkDirectory\depot_tools",
    "--url=$($source.repository)", "--branch=$($source.cef_branch)", "--checkout=$($source.revision)",
    "--chromium-checkout=$($source.chromium_tag)", '--no-chromium-history', '--x64-build',
    '--with-pgo-profiles', '--no-debug-build')
# Never use --force-update: it discards local source changes.
if ($SyncOnly) { $arguments += '--no-build', '--no-distrib' }
else { $arguments += '--force-build', '--force-distrib' }
if ($SkipUpdate) { $arguments += '--no-update' }
$started = [DateTime]::UtcNow
# PowerShell 5.1 treats redirected native stderr as errors under Stop.
$ErrorActionPreference = 'Continue'
& python (Join-Path $cefSource 'tools\automate\automate-git.py') @arguments 2>&1 | ForEach-Object { "$_" }
$code = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($code) { throw 'CEF sync/build failed' }
# Confirm what automate actually checked out, including the copy it builds.
$info = Read-SourceInfo 'skip'
if ($SyncOnly) { 'source-locked fork synced (no build)'; return }

$buildCef = $info.paths.build_checkout
$versionOutput = & python (Join-Path $buildCef 'tools\cef_version.py') current $chromiumSource
if ($LASTEXITCODE) { throw 'Cannot determine built CEF version' }
$cefVersion = ($versionOutput -join "`n").Trim()
if ($cefVersion -notmatch '^[A-Za-z0-9.+_-]+$') { throw "Invalid built CEF version: $cefVersion" }
if (!$AllowCandidate -and !$cefVersion.Contains("-$($source.branch).")) {
    throw "Built version $cefVersion is not named after '$($source.branch)'; refusing to publish it to .cache as a release engine."
}
$root = "cef_binary_$($cefVersion)_windows64"
$built = Join-Path (Join-Path $buildCef 'binary_distrib') "$root.tar.bz2"
if (!(Test-Path -LiteralPath $built)) { throw "Expected distribution not found: $built" }
if ((Get-Item -LiteralPath $built).LastWriteTimeUtc -lt $started.AddSeconds(-2)) {
    throw "Distribution predates this build: $built"
}
$target = Join-Path $repoRoot ".cache\$root-red.tar.bz2"
New-Item -ItemType Directory -Force (Split-Path -Parent $target) | Out-Null
$item = Publish-EngineArchive $built $target
$hash = $item.SHA256
"source repository: $($source.repository)"
"source revision: $($source.revision)"
"upstream revision: $($source.upstream_revision)"
"chromium revision: $($source.chromium_revision)"
"archive: $($item.Name)"
"root: $root"
"size: $($item.Length)"
"sha256: $hash"
'cmake/cef.lock.json is unchanged; pin this archive only after validation.'

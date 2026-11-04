# Applies Red's CEF/Chromium patches (cef-patches/*.patch) to a CEF checkout
# made by scripts/build_cef.ps1, then regenerates CEF's API wrappers.
# *.cef.patch applies inside chromium/src/cef, *.chromium.patch inside
# chromium/src. Already applied patches are skipped, so it can be re-run.
# Patches may stack (07 extends files 02 changed): when an earlier patch's
# context was changed by a later one it neither applies nor reverses alone, so
# the applied patches are rolled back newest first and all applied again.
# --ignore-whitespace: Git line-ending conversion (core.autocrlf) can make a
# checked-out patch differ from the checkout only in invisible CR/LF.
param([ValidatePattern('^[A-Za-z]:\\[^ ]+$')][string]$WorkDirectory = 'E:\cef-build')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$src = Join-Path $WorkDirectory 'code\chromium\src'
if (!(Test-Path -LiteralPath (Join-Path $src 'cef'))) { throw "No CEF checkout at $src" }
if (Test-Path -LiteralPath (Join-Path $src 'cef\RED_ENGINE.json')) {
    throw 'sol-156 already contains Red changes. Use its native tools/patcher.py; do not apply the legacy external patch stack over the fork.'
}
$ErrorActionPreference = 'Continue'
$patches = @(Get-ChildItem (Join-Path $repoRoot 'cef-patches') -Filter '*.patch' | Sort-Object Name |
    Where-Object { $_.Name -like '*.cef.patch' -or $_.Name -like '*.chromium.patch' })
function PatchDir($patch) { if ($patch.Name -like '*.cef.patch') { Join-Path $src 'cef' } else { $src } }
function Applied($patch) { git -C (PatchDir $patch) apply --ignore-whitespace --reverse --check $patch.FullName 2>$null; return $LASTEXITCODE -eq 0 }
function Applies($patch) { git -C (PatchDir $patch) apply --ignore-whitespace --check $patch.FullName 2>$null; return $LASTEXITCODE -eq 0 }

# Everything already applied: touch nothing, so nothing rebuilds. Checked per
# checkout by reversing the whole stack, newest first, on scratch copies of the
# files it touches (one `git apply --check` with several patches cannot verify
# patches that change the same file).
function StackApplied($dir, $stack) {
    $scratch = Join-Path ([IO.Path]::GetTempPath()) ("red-patch-check-" + [Guid]::NewGuid().ToString('N'))
    try {
        foreach ($patch in $stack) {
            foreach ($line in (git -C $dir apply --numstat $patch.FullName 2>$null)) {
                $file = ($line -split "`t")[2]
                $from = Join-Path $dir $file; $to = Join-Path $scratch $file
                if ((Test-Path -LiteralPath $from) -and !(Test-Path -LiteralPath $to)) {
                    New-Item -ItemType Directory -Force (Split-Path -Parent $to) | Out-Null
                    Copy-Item -LiteralPath $from -Destination $to
                }
            }
        }
        foreach ($patch in $stack) {
            # Outside any repository, git apply works on the plain files in the current directory.
            Push-Location $scratch
            git apply --ignore-whitespace --reverse $patch.FullName 2>$null
            $code = $LASTEXITCODE
            Pop-Location
            if ($code) { return $false }
        }
        return $true
    } finally { Remove-Item -Recurse -Force -LiteralPath $scratch -ErrorAction SilentlyContinue }
}
$complete = $true
foreach ($dir in ($patches | ForEach-Object { PatchDir $_ } | Sort-Object -Unique)) {
    $stack = @($patches | Where-Object { (PatchDir $_) -eq $dir } | Sort-Object Name -Descending)
    if (!(StackApplied $dir $stack)) { $complete = $false }
}
if ($complete) { $patches | ForEach-Object { "already applied: $($_.Name)" }; $patches = @() }
$stacked = $false
foreach ($patch in $patches) { if (!(Applied $patch) -and !(Applies $patch)) { $stacked = $true; break } }
if ($stacked) {
    "Stacked patches: rolling back applied patches, newest first"
    for ($i = $patches.Count - 1; $i -ge 0; --$i) {
        if (Applied $patches[$i]) {
            git -C (PatchDir $patches[$i]) apply --ignore-whitespace --reverse $patches[$i].FullName
            if ($LASTEXITCODE) { $ErrorActionPreference = 'Stop'; throw "Rollback failed: $($patches[$i].Name)" }
            "rolled back: $($patches[$i].Name)"
        }
    }
}
foreach ($patch in $patches) {
    if (Applied $patch) { "already applied: $($patch.Name)"; continue }
    git -C (PatchDir $patch) apply --ignore-whitespace --check $patch.FullName
    if ($LASTEXITCODE) { $ErrorActionPreference = 'Stop'; throw "Patch does not apply: $($patch.Name)" }
    git -C (PatchDir $patch) apply --ignore-whitespace $patch.FullName
    if ($LASTEXITCODE) { $ErrorActionPreference = 'Stop'; throw "Patch failed: $($patch.Name)" }
    "applied: $($patch.Name)"
}
# New API (added=experimental) needs the generated C API and wrappers.
Push-Location (Join-Path $src 'cef')
python tools\version_manager.py -u 2>&1 | ForEach-Object { "$_" }
$code = $LASTEXITCODE
Pop-Location
$ErrorActionPreference = 'Stop'
if ($code) { throw 'CEF API translation failed' }

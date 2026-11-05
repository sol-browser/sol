param([ValidateSet('Release','Debug')][string]$Configuration = 'Release',
      [ValidatePattern('^[A-Za-z0-9_-]*$')][string]$BuildDirectory = '')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildPath = if ($BuildDirectory) { "build/$BuildDirectory" } else { "build/$Configuration" }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (!(Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio with C++ tools is required' }
$vsRoot = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsRoot) { throw 'Install the Visual Studio Desktop development with C++ workload' }
Import-Module (Join-Path $vsRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vsRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$cmake = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninjaDir = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
$env:PATH = "$ninjaDir;$env:PATH"
Push-Location $repoRoot
try {
    python scripts/fetch_cef.py
    if ($LASTEXITCODE) { throw 'CEF setup failed' }
    python scripts/fetch_sqlite.py
    if ($LASTEXITCODE) { throw 'SQLite setup failed' }
    python scripts/fetch_velopack.py
    if ($LASTEXITCODE) { throw 'Velopack setup failed' }
    Push-Location apps/browser-ui
    try {
        & npm.cmd ci --no-audit --no-fund
        if ($LASTEXITCODE) { throw 'UI dependency installation failed' }
        # Bundler warnings go to stderr; under 'Stop' Windows PowerShell 5.1
        # would turn them into errors. Judge the build by its exit code.
        $ErrorActionPreference = 'Continue'
        & npm.cmd run build 2>&1 | ForEach-Object { "$_" }
        $uiCode = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        if ($uiCode) { throw 'UI build failed' }
        & node --experimental-strip-types --test ../../tests/contracts.test.mjs ../../tests/welcome.test.mjs ../../tests/welcome-sound.test.mjs ../../tests/sidebar.test.mjs ../../tests/navigation.test.mjs ../../tests/command-model.test.mjs ../../tests/downloads.test.mjs ../../tests/site.test.mjs ../../tests/history.test.mjs ../../tests/downloads-page.test.mjs ../../tests/theme.test.mjs ../../tests/favicon-color.test.mjs
        if ($LASTEXITCODE) { throw 'Protocol tests failed' }
    } finally { Pop-Location }
    & $cmake -S . -B $buildPath -G Ninja "-DCMAKE_BUILD_TYPE=$Configuration"
    if ($LASTEXITCODE) { throw 'CMake configuration failed' }
    & $cmake --build $buildPath --parallel 6
    if ($LASTEXITCODE) { throw 'Native build failed' }
    $ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
    & $ctest --test-dir $buildPath --output-on-failure -C $Configuration
    if ($LASTEXITCODE) { throw 'Native tests failed' }
    $previousTestExe = $env:RED_BROWSER_TEST_EXE
    $env:RED_BROWSER_TEST_EXE = Join-Path $repoRoot "$buildPath/apps/desktop/$Configuration/sol.exe"
    # unittest -v reports on stderr. With 'Stop', Windows PowerShell 5.1 turns
    # redirected native stderr into a terminating error even when tests pass;
    # judge the run by its exit code instead.
    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { python -m unittest discover -s tests -p 'test_*.py' -v 2>&1 | ForEach-Object { "$_" } }
    finally { $ErrorActionPreference = $previousPreference; $env:RED_BROWSER_TEST_EXE = $previousTestExe }
    if ($LASTEXITCODE) { throw 'Dependency or CLI tests failed' }
} finally { Pop-Location }

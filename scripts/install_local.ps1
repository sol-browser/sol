# Copies a finished build to one fixed folder the person launches Sol from:
# the Velopack install's %LOCALAPPDATA%\SolBrowser\current when it exists, else
# %LOCALAPPDATA%\Sol\Application (the pre-Velopack location). Windows Firewall
# (and other per-path OS state) remembers a program by its executable path, so
# launching every build from its own build/<name> folder made Windows ask again
# for each one. The browser profile (User Data) is untouched. In the Velopack
# folder sq.version stays, so Velopack still knows the installed version and a
# published update replaces the development build.
param([Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$BuildDirectory,
      [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$source = Join-Path $repoRoot "build/$BuildDirectory/apps/desktop/$Configuration"
if (!(Test-Path -LiteralPath (Join-Path $source 'sol.exe'))) { throw "No built browser in $source" }
if (!(Test-Path -LiteralPath (Join-Path $source 'ui/index.html'))) { throw "Build has no UI assets: $source" }
$velopack = Join-Path $env:LOCALAPPDATA 'SolBrowser\current'
$target = if (Test-Path -LiteralPath (Join-Path $velopack 'sq.version')) { $velopack } else { Join-Path $env:LOCALAPPDATA 'Sol\Application' }
$exe = Join-Path $target 'sol.exe'
# Never replace files under a running browser.
$running = Get-CimInstance Win32_Process -Filter "Name='sol.exe'" |
  Where-Object { $_.ExecutablePath -and $_.ExecutablePath -ieq $exe }
if ($running) { throw "Sol is running from $target (PID $($running[0].ProcessId)); close it first" }
New-Item -ItemType Directory -Force -Path $target | Out-Null
# Mirror the build output, skipping debug symbols (and keeping Velopack's sq.version).
& robocopy $source $target /MIR /XF *.pdb *.ilk sq.version /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "Copy failed (robocopy $LASTEXITCODE)" }
# Same ACL the build applies (cef_macros.cmake SET_LPAC_ACLS): sandboxed LPAC
# processes must be able to read and execute the browser's own files.
& icacls $target /grant '*S-1-15-2-2:(OI)(CI)(RX)' | Out-Null
if ($LASTEXITCODE) { throw 'Could not set sandbox ACL on the install folder' }
Write-Output $exe

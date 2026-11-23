[CmdletBinding(DefaultParameterSetName = 'Prepare')]
param(
    [Parameter(Mandatory)][string]$Runtime,
    [Parameter(Mandatory, ParameterSetName = 'Prepare')][string]$EcsArchive,
    [Parameter(ParameterSetName = 'Prepare')][string]$Application,
    [Parameter(Mandatory, ParameterSetName = 'Install')][string]$SignedPackage
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
# The component's source lives in sol-browser/streaming-drm; development
# builds use a checkout next to this repository (releases come from its CI).
if (!$Application) { $Application = Join-Path (Split-Path $root -Parent) 'streaming-drm/app' }
$runtimePath = (Resolve-Path -LiteralPath $Runtime).ProviderPath
$destination = Join-Path $runtimePath 'protected-media'
if (Test-Path -LiteralPath $destination) { throw "Protected Media is already staged at $destination" }
$tools = Join-Path $root '.cache/protected-media-tools'
$modules = Join-Path $tools 'node_modules'
$packager = Join-Path $modules '@electron/packager/bin/electron-packager.mjs'
if (!(Test-Path -LiteralPath $packager) -or
    !(Test-Path -LiteralPath (Join-Path $modules '@electron/fuses/dist/bin.js'))) {
    npm.cmd install --no-save --no-package-lock --ignore-scripts --prefix $tools '@electron/packager@19.1.1' '@electron/fuses@2.1.3'
    if ($LASTEXITCODE -ne 0) { throw 'Unable to install Electron packaging tools' }
}
$fuses = Join-Path $modules '@electron/fuses/dist/bin.js'
if ($SignedPackage) {
    $source = (Resolve-Path -LiteralPath $SignedPackage).ProviderPath
    $receipt = python -m castlabs_evs.vmp --no-ask verify-pkg $source
    if ($LASTEXITCODE -ne 0 -or ($receipt -join ' ') -notmatch 'Signature is valid:\s+streaming') {
        throw "A streaming VMP signature is required for $source"
    }
    $enabled = node $fuses read --app (Join-Path $source 'SolProtectedMedia.exe')
    $fuseStatus = ($enabled -join ' ') -replace '\x1b\[[0-9;]*m'
    if ($LASTEXITCODE -ne 0 -or $fuseStatus -notmatch 'EnableCookieEncryption is Enabled' -or
        $fuseStatus -notmatch 'EnableEmbeddedAsarIntegrityValidation is Enabled' -or
        $fuseStatus -notmatch 'OnlyLoadAppFromAsar is Enabled' -or
        !(Test-Path -LiteralPath (Join-Path $source 'resources/app.asar'))) {
        throw "Expected Protected Media with cookie encryption and an integrity-checked app.asar at $source"
    }
    $stage = Join-Path $runtimePath ('protected-media-install-' + [guid]::NewGuid().ToString('N'))
    Copy-Item -LiteralPath $source -Destination $stage -Recurse
    [IO.Directory]::Move($stage, $destination)
    Write-Output $destination
    return
}
$archive = (Resolve-Path -LiteralPath $EcsArchive).ProviderPath
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne 'a17f87a95a62e8f336d6fa18e57b9bc83d513b02211e4e5771a45cfd7482740e') {
    throw 'Expected the pinned castLabs ECS 45.2.1+wvcus archive for Windows x64'
}
$work = Join-Path $runtimePath ('protected-media-stage-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
$zip = Join-Path $work 'electron-v45.2.1-win32-x64.zip'
Copy-Item -LiteralPath $archive -Destination $zip
$packArgs = @('--platform=win32', '--arch=x64', '--electron-version=45.2.1', "--electron-zip-dir=$work",
    '--asar', "--out=$work", "--tmpdir=$work", '--no-prune',
    # Windows shows FileDescription as the process name (Task Manager, volume
    # mixer, media controls) and the exe icon beside it. Branding changes the
    # exe, so it happens here, before the VMP signature is applied.
    "--icon=$(Join-Path $root 'apps/desktop/resources/red.ico')",
    '--win32metadata.FileDescription=Sol', '--win32metadata.ProductName=Sol', '--win32metadata.CompanyName=Sol',
    '--win32metadata.InternalName=SolProtectedMedia', '--win32metadata.OriginalFilename=SolProtectedMedia.exe',
    '--app-copyright=Sol browser')
node $packager (Resolve-Path -LiteralPath $Application).ProviderPath SolProtectedMedia @packArgs
if ($LASTEXITCODE -ne 0) { throw 'Unable to package Protected Media' }

$package = Join-Path $work 'SolProtectedMedia-win32-x64'
$exe = Join-Path $package 'SolProtectedMedia.exe'
$fuseArgs = @('RunAsNode=off', 'EnableCookieEncryption=on', 'EnableNodeOptionsEnvironmentVariable=off',
    'EnableNodeCliInspectArguments=off', 'EnableEmbeddedAsarIntegrityValidation=on', 'OnlyLoadAppFromAsar=on',
    'LoadBrowserProcessSpecificV8Snapshot=off', 'GrantFileProtocolExtraPrivileges=on')
node $fuses write --app $exe @fuseArgs
if ($LASTEXITCODE -ne 0) { throw 'Unable to apply the castLabs-supported fuse configuration' }

Remove-Item -LiteralPath $zip
Write-Output $package

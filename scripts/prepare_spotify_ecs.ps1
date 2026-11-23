# Stage a clean package for the development control or for subsequent VMP signing.
param([ValidateSet('development', 'production')][string]$Variant = 'development')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$cache = Join-Path $root '.cache'
$archive = Join-Path $cache 'ecs-45.2.1-win-x64.zip'
$target = Join-Path $cache $(if ($Variant -eq 'production') { 'spotify-ecs-production' } else { 'spotify-ecs-development' })
if (Test-Path -LiteralPath $target) { throw "Package already exists at $target" }
New-Item -ItemType Directory -Force -Path $cache | Out-Null
if (!(Test-Path -LiteralPath $archive)) {
    $url = 'https://github.com/castlabs/electron-releases/releases/download/v45.2.1%2Bwvcus/electron-v45.2.1%2Bwvcus-win32-x64.zip'
    Invoke-WebRequest -Uri $url -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne 'a17f87a95a62e8f336d6fa18e57b9bc83d513b02211e4e5771a45cfd7482740e') {
    throw "Invalid ECS archive hash at $archive"
}
$stage = Join-Path $cache ('spotify-ecs-stage-' + [guid]::NewGuid().ToString('N'))
Expand-Archive -LiteralPath $archive -DestinationPath $stage
Rename-Item -LiteralPath (Join-Path $stage 'electron.exe') -NewName 'SolSpotifyProbe.exe'
Rename-Item -LiteralPath (Join-Path $stage 'electron.exe.sig') -NewName 'SolSpotifyProbe.exe.sig'
$app = New-Item -ItemType Directory -Path (Join-Path $stage 'resources/app')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'spotify_ecs/package.json'),(Join-Path $PSScriptRoot 'spotify_ecs/main.cjs') -Destination $app.FullName
[IO.Directory]::Move($stage, $target)
Write-Output $target

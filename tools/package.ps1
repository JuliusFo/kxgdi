<#
.SYNOPSIS
    Builds, tests and packs dist\kxgdi-win7-x64.zip for Windows 7 machines.
#>
param(
    # Build server mode, passed on to build.ps1.
    [switch] $CI
)

$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent

& "$root\build.ps1" -Test -CI:$CI

$stage = Join-Path $root 'dist\kxgdi-win7-x64'
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null

$files = @(
    'out\x64\kxgdi.dll',
    'out\x64\kxgdildr.dll',
    'out\x64\kxgdi-test.exe',
    'out\x64\kxgdi-ldrtest.exe',
    'out\x64\kxgdi-ldrtest-static.dll',
    'out\x64\kxgdi-ldrtest-dynamic.dll',
    'install\Install-KxGdi.ps1',
    'test\Test-Loader.ps1',
    'README.md',
    'README.de.md',
    'CHANGELOG.md',
    'LICENSE'
)
foreach ($f in $files) { Copy-Item (Join-Path $root $f) $stage }

$zip = Join-Path $root 'dist\kxgdi-win7-x64.zip'
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Remove-Item $stage -Recurse -Force

Write-Host "Package: $zip"

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

Copy-Item "$root\out\x64\kxgdi.dll"             $stage
Copy-Item "$root\out\x64\kxgdi-test.exe"        $stage
Copy-Item "$root\install\Install-KxGdi.ps1"     $stage
Copy-Item "$root\README.md"                     $stage
Copy-Item "$root\README.de.md"                  $stage
Copy-Item "$root\LICENSE"                       $stage

$zip = Join-Path $root 'dist\kxgdi-win7-x64.zip'
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$stage\*" -DestinationPath $zip
Remove-Item $stage -Recurse -Force

Write-Host "Package: $zip"

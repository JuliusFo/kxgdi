<#
.SYNOPSIS
    End-to-end test of the standalone loader (kxgdildr.dll).
    Needs administrator rights. Compatible with PowerShell 2.0 (Windows 7).

.DESCRIPTION
    1. Puts kxgdildr.dll and kxgdi.dll into System32 (an existing different
       version is set aside and restored afterwards).
    2. Registers kxgdildr.dll as verifier provider for kxgdi-ldrtest.exe only.
    3. Runs kxgdi-ldrtest.exe, which imports D3DKMTOpenAdapterFromLuid from
       gdi32.dll like Path of Exile does.
    4. Removes everything again, even if the test fails.

    Exit code = exit code of kxgdi-ldrtest.exe (0 = all checks passed).

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File .\Test-Loader.ps1
#>
param(
    # Folder with kxgdildr.dll, kxgdi.dll, kxgdi-ldrtest.exe and its DLLs.
    [string] $Dir
)

$ErrorActionPreference = 'Stop'
if (-not $Dir) { $Dir = Split-Path -Parent $MyInvocation.MyCommand.Path }

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host 'ERROR: Please start PowerShell with "Run as administrator".' -ForegroundColor Red
    exit 1
}
if ([IntPtr]::Size -ne 8) {
    Write-Host 'ERROR: Please use the 64-bit PowerShell (not "Windows PowerShell (x86)").' -ForegroundColor Red
    exit 1
}

$System32 = Join-Path $env:windir 'System32'
$IfeoKey  = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options\kxgdi-ldrtest.exe'
$TestExe  = Join-Path $Dir 'kxgdi-ldrtest.exe'

function Get-Sha256([string] $Path) {
    $sha = New-Object Security.Cryptography.SHA256Managed
    $stream = [IO.File]::OpenRead($Path)
    try { return [BitConverter]::ToString($sha.ComputeHash($stream)) } finally { $stream.Close() }
}

foreach ($f in 'kxgdildr.dll', 'kxgdi.dll', 'kxgdi-ldrtest.exe', 'kxgdi-ldrtest-static.dll', 'kxgdi-ldrtest-dynamic.dll') {
    if (-not (Test-Path (Join-Path $Dir $f))) {
        Write-Host "ERROR: $f not found in $Dir" -ForegroundColor Red
        exit 1
    }
}

$copied   = @()   # files we put into System32
$setAside = @()   # existing files we renamed and must restore
$code = 1

try {
    foreach ($f in 'kxgdildr.dll', 'kxgdi.dll') {
        $src  = Join-Path $Dir $f
        $dest = Join-Path $System32 $f

        if (Test-Path $dest) {
            if ((Get-Sha256 $src) -eq (Get-Sha256 $dest)) { continue }
            $backup = "$dest.kxgdi-test-backup"
            if (Test-Path $backup) { Remove-Item $backup -Force }
            Rename-Item $dest $backup
            $setAside += $dest
        }

        Copy-Item $src $dest
        $copied += $dest
    }

    if (-not (Test-Path $IfeoKey)) { New-Item $IfeoKey -Force | Out-Null }
    New-ItemProperty $IfeoKey -Name 'GlobalFlag'    -PropertyType DWord  -Value 0x100 -Force | Out-Null
    New-ItemProperty $IfeoKey -Name 'VerifierFlags' -PropertyType DWord  -Value ([int] -2147483648) -Force | Out-Null
    New-ItemProperty $IfeoKey -Name 'VerifierDlls'  -PropertyType String -Value 'kxgdildr.dll' -Force | Out-Null

    Push-Location $Dir
    try {
        & $TestExe
        $code = $LASTEXITCODE
    } finally {
        Pop-Location
    }
} finally {
    if (Test-Path $IfeoKey) { Remove-Item $IfeoKey -Recurse -Force }

    foreach ($dest in $copied) {
        if (Test-Path $dest) { Remove-Item $dest -Force }
    }
    foreach ($dest in $setAside) {
        Rename-Item "$dest.kxgdi-test-backup" (Split-Path $dest -Leaf)
    }
}

if ($code -eq 0) {
    Write-Host 'Loader test passed. Nothing was left behind in System32 or the registry.' -ForegroundColor Green
} else {
    Write-Host "Loader test FAILED (exit code $code). System32 and the registry were cleaned up." -ForegroundColor Red
}
exit $code

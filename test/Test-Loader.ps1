<#
.SYNOPSIS
    End-to-end test of the standalone loader (kxgdildr.dll).
    Needs administrator rights. Compatible with PowerShell 2.0 (Windows 7).

.DESCRIPTION
    1. Puts kxgdildr.dll and kxgdi.dll into System32 (an existing different
       version is set aside and restored afterwards).
    2. Copies the test program to a temporary folder without kxgdi.dll, so
       kxgdi.dll is loaded from System32 like in the installed setup.
    3. Registers kxgdildr.dll as verifier provider for kxgdi-ldrtest.exe only.
    4. Runs kxgdi-ldrtest.exe, which imports D3DKMTOpenAdapterFromLuid from
       gdi32.dll like Path of Exile does, and shows the loader log file.
    5. Removes everything again, even if the test fails.

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
$LogFile  = Join-Path $env:windir 'Temp\kxgdildr-kxgdi-ldrtest.exe.log'
$RunDir   = Join-Path ([IO.Path]::GetTempPath()) ('kxgdi-ldrtest-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))
$TestFiles = @('kxgdi-ldrtest.exe', 'kxgdi-ldrtest-static.dll', 'kxgdi-ldrtest-dynamic.dll')

function Get-Sha256([string] $Path) {
    $sha = New-Object Security.Cryptography.SHA256Managed
    $stream = [IO.File]::OpenRead($Path)
    try { return [BitConverter]::ToString($sha.ComputeHash($stream)) } finally { $stream.Close() }
}

foreach ($f in (@('kxgdildr.dll', 'kxgdi.dll') + $TestFiles)) {
    if (-not (Test-Path -LiteralPath (Join-Path $Dir $f))) {
        Write-Host "ERROR: $f not found in $Dir" -ForegroundColor Red
        exit 1
    }
}

$copied   = @()   # files we put into System32
$setAside = @()   # existing System32 files we renamed and must restore
$problems = @()
$code = 1

try {
    foreach ($f in 'kxgdildr.dll', 'kxgdi.dll') {
        $src  = Join-Path $Dir $f
        $dest = Join-Path $System32 $f

        if (Test-Path $dest) {
            if ((Get-Sha256 $src) -eq (Get-Sha256 $dest)) { continue }
            $backup = "$dest.kxgdi-test-backup"
            if (Test-Path $backup) { Remove-Item $backup -Force }
            Rename-Item $dest (Split-Path $backup -Leaf)
            $setAside += $dest
        }

        Copy-Item -LiteralPath $src $dest
        $copied += $dest
    }

    New-Item -ItemType Directory $RunDir | Out-Null
    foreach ($f in $TestFiles) { Copy-Item -LiteralPath (Join-Path $Dir $f) $RunDir }

    if (Test-Path $LogFile) { Remove-Item $LogFile -Force }

    if (-not (Test-Path $IfeoKey)) { New-Item $IfeoKey -Force | Out-Null }
    New-ItemProperty $IfeoKey -Name 'GlobalFlag'    -PropertyType DWord  -Value 0x100 -Force | Out-Null
    New-ItemProperty $IfeoKey -Name 'VerifierFlags' -PropertyType DWord  -Value ([int] -2147483648) -Force | Out-Null
    New-ItemProperty $IfeoKey -Name 'VerifierDlls'  -PropertyType String -Value 'kxgdildr.dll' -Force | Out-Null

    Push-Location $RunDir
    try {
        & (Join-Path $RunDir 'kxgdi-ldrtest.exe')
        $code = $LASTEXITCODE
    } finally {
        Pop-Location
    }

    Write-Host "--- loader log file ($LogFile) ---"
    if (Test-Path $LogFile) { Get-Content $LogFile | ForEach-Object { Write-Host $_ } } else { Write-Host '(not found)' }
    Write-Host '-----------------------------------'
} finally {
    # Each step on its own: one failure must not skip the others. Registry
    # first, so no program refers to a provider that is gone.
    try { if (Test-Path $IfeoKey) { Remove-Item $IfeoKey -Recurse -Force } }
    catch { $problems += "Could not remove $IfeoKey : $_" }

    foreach ($dest in $copied) {
        try { if (Test-Path $dest) { Remove-Item $dest -Force } }
        catch { $problems += "Could not remove $dest : $_" }
    }

    foreach ($dest in $setAside) {
        try {
            if (Test-Path $dest) {
                $problems += "Could not restore $dest.kxgdi-test-backup because $dest still exists"
            } else {
                Rename-Item "$dest.kxgdi-test-backup" (Split-Path $dest -Leaf)
            }
        } catch { $problems += "Could not restore $dest : $_" }
    }

    try { if (Test-Path $RunDir) { Remove-Item $RunDir -Recurse -Force } }
    catch { $problems += "Could not remove $RunDir : $_" }
}

foreach ($p in $problems) { Write-Host "WARNING: $p" -ForegroundColor Yellow }

if ($code -eq 0) {
    Write-Host 'Loader test passed.' -ForegroundColor Green
} else {
    Write-Host "Loader test FAILED (exit code $code)." -ForegroundColor Red
}
if ($problems.Count -eq 0) {
    Write-Host 'Nothing was left behind in System32 or the registry.'
} elseif ($code -eq 0) {
    $code = 100     # test passed, but cleanup left something behind
}
exit $code

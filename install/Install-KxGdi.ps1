<#
.SYNOPSIS
    Installs kxgdi.dll into VxKex and enables the gdi32 -> kxgdi rewrite for
    the given programs. Compatible with PowerShell 2.0 (Windows 7).

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File .\Install-KxGdi.ps1 -ExePath "C:\Program Files (x86)\Steam\steamapps\common\Path of Exile\PathOfExileSteam.exe"

.EXAMPLE
    # Several programs at once
    .\Install-KxGdi.ps1 -ExePath "C:\Games\Foo\a.exe","C:\Games\Foo\b.tmp"

.EXAMPLE
    # Undo: removes the rewrite entries and kxgdi.dll (VxKex itself stays enabled)
    .\Install-KxGdi.ps1 -ExePath "...\PathOfExileSteam.exe" -Uninstall
#>
param(
    [Parameter(Mandatory = $true)]
    [string[]] $ExePath,
    [switch]   $Uninstall
)

$ErrorActionPreference = 'Stop'

# Rewrite entries added by kxgdi. Replacement names must not be longer than
# the original DLL names (VxKex rewrites import names in place).
$OurEntries = @('gdi32:kxgdi', 'api-ms-win-dx-d3dkmt:kxgdi')
$OurKeys    = @('gdi32', 'api-ms-win-dx-d3dkmt')
$IfeoRoot   = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options'

function Fail($Message) {
    Write-Host "ERROR: $Message" -ForegroundColor Red
    exit 1
}

#
# Preconditions
#

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Fail 'Please start PowerShell with "Run as administrator".'
}

if ([IntPtr]::Size -ne 8) {
    Fail 'Please use the 64-bit PowerShell (not "Windows PowerShell (x86)").'
}

$vxkex = Get-ItemProperty 'HKLM:\SOFTWARE\VXsoft\VxKex' -ErrorAction SilentlyContinue
if (-not $vxkex -or -not $vxkex.KexDir) {
    Fail 'VxKex is not installed (registry value HKLM\SOFTWARE\VXsoft\VxKex\KexDir is missing). Version 1.2.0 or later is required.'
}

$KexDir   = $vxkex.KexDir
$Kex64    = Join-Path $KexDir 'Kex64'
$KexCfg   = Join-Path $KexDir 'KexCfg.exe'
$DllSrc   = Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) 'kxgdi.dll'
$DllDest  = Join-Path $Kex64 'kxgdi.dll'

Write-Host "VxKex directory: $KexDir"

#
# Helpers
#

function Get-FilterKey([string] $Exe) {
    # VxKex stores per-program settings in an IFEO subkey whose FilterFullPath
    # value is the full path of the program.
    $name = [IO.Path]::GetFileName($Exe)
    $base = Join-Path $IfeoRoot $name
    if (-not (Test-Path $base)) { return $null }

    foreach ($sub in (Get-ChildItem $base -ErrorAction SilentlyContinue)) {
        $props = Get-ItemProperty $sub.PSPath -ErrorAction SilentlyContinue
        if ($props -and $props.FilterFullPath -and ($props.FilterFullPath -eq $Exe)) {
            return $sub.PSPath
        }
    }

    return $null
}

function Split-Entries([string] $Value) {
    $result = @()
    if ($Value) {
        foreach ($e in $Value.Split('|')) {
            if ($e.Trim() -ne '') { $result += $e.Trim() }
        }
    }
    return ,$result
}

function Remove-OurKeys([string[]] $Entries) {
    $result = @()
    foreach ($e in $Entries) {
        $key = $e.Split(':')[0].Trim()
        if (-not ($OurKeys -contains $key.ToLower())) { $result += $e }
    }
    return ,$result
}

#
# Uninstall
#

if ($Uninstall) {
    foreach ($exe in $ExePath) {
        $key = Get-FilterKey $exe
        if (-not $key) {
            Write-Host "No VxKex settings found for: $exe"
            continue
        }

        $current = (Get-ItemProperty $key).KEX_DllRewriteEntries
        $rest = Remove-OurKeys (Split-Entries $current)

        if ($rest.Count -gt 0) {
            Set-ItemProperty $key -Name 'KEX_DllRewriteEntries' -Value ($rest -join '|')
        } else {
            Remove-ItemProperty $key -Name 'KEX_DllRewriteEntries' -ErrorAction SilentlyContinue
        }

        Write-Host "Rewrite removed for: $exe"
    }

    if (Test-Path $DllDest) {
        Remove-Item $DllDest -Force
        Write-Host "Deleted: $DllDest"
    }

    Write-Host ''
    Write-Host 'Done. VxKex itself stays enabled for these programs (to disable: Properties -> VxKex tab).'
    exit 0
}

#
# Install
#

if (-not (Test-Path $DllSrc)) {
    Fail "kxgdi.dll not found next to the script: $DllSrc"
}

if (-not (Test-Path $Kex64)) {
    Fail "Directory not found: $Kex64 (is this a 64-bit Windows with 64-bit VxKex?)"
}

Copy-Item $DllSrc $DllDest -Force
Write-Host "Copied: $DllDest"

foreach ($exe in $ExePath) {
    Write-Host ''
    Write-Host "Program: $exe"

    if (-not (Test-Path $exe)) {
        Write-Host '  Note: the file does not exist right now (e.g. a temporary .tmp file). Continuing anyway.' -ForegroundColor Yellow
    }

    # Enable VxKex for the program (creates the IFEO filter key).
    if (Test-Path $KexCfg) {
        $p = Start-Process -FilePath $KexCfg -ArgumentList "/EXE:`"$exe`" /ENABLE:1" -Wait -PassThru
        if ($p.ExitCode -ne 0) {
            Write-Host "  Warning: KexCfg.exe returned exit code $($p.ExitCode)." -ForegroundColor Yellow
        }
    } else {
        Write-Host "  Warning: $KexCfg not found. Please enable VxKex manually (Properties -> VxKex tab)." -ForegroundColor Yellow
    }

    $key = Get-FilterKey $exe
    if (-not $key) {
        Write-Host '  ERROR: No VxKex settings found. Please enable VxKex for this program in the' -ForegroundColor Red
        Write-Host '  Properties dialog (VxKex tab) and run this script again.' -ForegroundColor Red
        continue
    }

    $current = (Get-ItemProperty $key).KEX_DllRewriteEntries
    $entries = Remove-OurKeys (Split-Entries $current)
    $entries += $OurEntries
    $value = $entries -join '|'

    Set-ItemProperty $key -Name 'KEX_DllRewriteEntries' -Value $value
    Write-Host "  KEX_DllRewriteEntries = $value" -ForegroundColor Green
}

Write-Host ''
Write-Host 'Done. To watch kxgdi messages, start Sysinternals DebugView (lines starting with "KxGdi:").'

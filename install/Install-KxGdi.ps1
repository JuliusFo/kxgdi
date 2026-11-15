<#
.SYNOPSIS
    Installs kxgdi for the given programs. Compatible with PowerShell 2.0 (Windows 7).

.DESCRIPTION
    Two methods:

    -Method Loader (default)
        Works without VxKex, and alongside any VxKex version (also VxKex NEXT).
        Copies kxgdildr.dll and kxgdi.dll to System32 and registers kxgdildr.dll
        as Application Verifier provider for the given programs (IFEO values
        GlobalFlag, VerifierFlags, VerifierDlls). If VxKex is enabled for a
        program, kxgdildr.dll is added after KexDll.dll in VxKex's settings.

    -Method VxKex
        Needs VxKex 1.2.0 or later from i486/VxKex (not VxKex NEXT).
        Copies kxgdi.dll to <VxKex>\Kex64, enables VxKex for the programs and
        sets KEX_DllRewriteEntries = gdi32:kxgdi|api-ms-win-dx-d3dkmt:kxgdi.

.EXAMPLE
    powershell -NoProfile -ExecutionPolicy Bypass -File .\Install-KxGdi.ps1 -ExePath "C:\Program Files (x86)\Steam\steamapps\common\Path of Exile\PathOfExileSteam.exe","C:\...\PathOfExile.tmp"

.EXAMPLE
    # Undo (use the same -Method as for the installation)
    .\Install-KxGdi.ps1 -ExePath "...\PathOfExileSteam.exe","...\PathOfExile.tmp" -Uninstall
#>
param(
    [Parameter(Mandatory = $true)]
    [string[]] $ExePath,
    [ValidateSet('Loader', 'VxKex')]
    [string]   $Method = 'Loader',
    [switch]   $Uninstall,
    # Install even if this is not Windows 7 (only useful for testing).
    [switch]   $Force
)

$ErrorActionPreference = 'Stop'

$IfeoRoot  = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options'
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

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

$os = [Environment]::OSVersion.Version
if (-not $Uninstall -and -not ($os.Major -eq 6 -and $os.Minor -eq 1) -and -not $Force) {
    # Newer Windows versions have D3DKMTOpenAdapterFromLuid natively, and the
    # loader only switches Application Verifier off again on Windows 7.
    Fail "kxgdi is only needed on Windows 7 (this is Windows $($os.Major).$($os.Minor)). Use -Force to install anyway."
}

#
# Helpers (shared)
#

function Get-NormalizedPath([string] $Path) {
    # Relative paths are relative to the PowerShell location, not the process directory.
    try {
        if (-not [IO.Path]::IsPathRooted($Path)) { $Path = Join-Path (Get-Location).ProviderPath $Path }
        return [IO.Path]::GetFullPath($Path)
    } catch {
        return $Path
    }
}

function Get-FilterKey([string] $Exe) {
    # VxKex stores per-program settings in an IFEO subkey whose FilterFullPath
    # value is the full path of the program.
    $name = [IO.Path]::GetFileName($Exe)
    $base = Join-Path $IfeoRoot $name
    if (-not (Test-Path $base)) { return $null }

    $full = Get-NormalizedPath $Exe
    foreach ($sub in (Get-ChildItem $base -ErrorAction SilentlyContinue)) {
        $props = Get-ItemProperty $sub.PSPath -ErrorAction SilentlyContinue
        if ($props -and $props.FilterFullPath -and ((Get-NormalizedPath $props.FilterFullPath) -eq $full)) {
            return $sub.PSPath
        }
    }

    return $null
}

# Other filter subkeys (other paths of a program with the same file name).
function Get-OtherFilterPaths([string] $Exe) {
    $base = Join-Path $IfeoRoot ([IO.Path]::GetFileName($Exe))
    $result = @()
    if (Test-Path $base) {
        foreach ($sub in (Get-ChildItem $base -ErrorAction SilentlyContinue)) {
            $props = Get-ItemProperty $sub.PSPath -ErrorAction SilentlyContinue
            if ($props -and $props.FilterFullPath) { $result += $props.FilterFullPath }
        }
    }
    return ,$result
}

function Get-Value($Key, [string] $Name) {
    $props = Get-ItemProperty $Key -ErrorAction SilentlyContinue
    if ($props -and ($props.PSObject.Properties.Match($Name).Count -gt 0)) { return $props.$Name }
    return $null
}

function ConvertTo-Int($Value) {
    # GlobalFlag may be a DWORD or a string like "0x00000100".
    if ($Value -eq $null) { return 0 }
    if ($Value -is [string]) {
        $s = $Value.Trim()
        if ($s.StartsWith('0x')) { return [Convert]::ToInt32($s.Substring(2), 16) }
        if ($s -eq '') { return 0 }
        return [int] $s
    }
    return [int] $Value
}

#
# Method: Loader
#

$LoaderDll   = 'kxgdildr.dll'
$System32    = Join-Path $env:windir 'System32'
$FlagVerifier = 0x100

function Split-Dlls([string] $Value) {
    $result = @()
    if ($Value) {
        foreach ($t in ($Value -split '\s+')) { if ($t -ne '') { $result += $t } }
    }
    return ,$result
}

function Remove-LoaderToken([string[]] $Tokens) {
    $result = @()
    foreach ($t in $Tokens) { if ($t.ToLower() -ne $LoaderDll) { $result += $t } }
    return ,$result
}

function Add-LoaderEntry($Key) {
    # Order: KexDll.dll (VxKex) first, then other providers, kxgdildr.dll last.
    # kxgdildr relies on VxKex having run first to leave Application Verifier
    # to it.
    $tokens = @()
    $others = Remove-LoaderToken (Split-Dlls (Get-Value $Key 'VerifierDlls'))
    foreach ($t in $others) { if ($t.ToLower() -eq 'kexdll.dll') { $tokens += $t } }
    foreach ($t in $others) { if ($t.ToLower() -ne 'kexdll.dll') { $tokens += $t } }
    $tokens += $LoaderDll
    Set-ItemProperty $Key -Name 'VerifierDlls' -Value ($tokens -join ' ')

    $flag = ConvertTo-Int (Get-Value $Key 'GlobalFlag')
    if (($flag -band $FlagVerifier) -eq 0) {
        Remove-ItemProperty $Key -Name 'GlobalFlag' -ErrorAction SilentlyContinue
        New-ItemProperty $Key -Name 'GlobalFlag' -PropertyType DWord -Value ($flag -bor $FlagVerifier) | Out-Null
        New-ItemProperty $Key -Name 'KxGdiSetGlobalFlag' -PropertyType DWord -Value 1 -Force | Out-Null
    }

    if ((Get-Value $Key 'VerifierFlags') -eq $null) {
        New-ItemProperty $Key -Name 'VerifierFlags' -PropertyType DWord -Value ([int] -2147483648) | Out-Null
        New-ItemProperty $Key -Name 'KxGdiSetVerifierFlags' -PropertyType DWord -Value 1 -Force | Out-Null
    }

    return ($tokens -join ' ')
}

# Returns $true if the key referenced kxgdildr.dll.
function Remove-LoaderEntry($Key) {
    $before = Split-Dlls (Get-Value $Key 'VerifierDlls')
    $after  = Remove-LoaderToken $before
    if ($before.Count -eq $after.Count) { return $false }

    if ($after.Count -gt 0) {
        # Another provider (e.g. VxKex) still needs the verifier settings.
        Set-ItemProperty $Key -Name 'VerifierDlls' -Value ($after -join ' ')
        return $true
    }

    Remove-ItemProperty $Key -Name 'VerifierDlls' -ErrorAction SilentlyContinue

    if ((Get-Value $Key 'KxGdiSetGlobalFlag') -ne $null) {
        $flag = (ConvertTo-Int (Get-Value $Key 'GlobalFlag')) -band (-bnot $FlagVerifier)
        Remove-ItemProperty $Key -Name 'GlobalFlag' -ErrorAction SilentlyContinue
        if ($flag -ne 0) { New-ItemProperty $Key -Name 'GlobalFlag' -PropertyType DWord -Value $flag | Out-Null }
        Remove-ItemProperty $Key -Name 'KxGdiSetGlobalFlag' -ErrorAction SilentlyContinue
    }

    if ((Get-Value $Key 'KxGdiSetVerifierFlags') -ne $null) {
        Remove-ItemProperty $Key -Name 'VerifierFlags' -ErrorAction SilentlyContinue
        Remove-ItemProperty $Key -Name 'KxGdiSetVerifierFlags' -ErrorAction SilentlyContinue
    }

    return $true
}

function Test-LoaderStillUsed {
    foreach ($key in (Get-ChildItem $IfeoRoot -ErrorAction SilentlyContinue)) {
        $keys = @($key.PSPath) + @(Get-ChildItem $key.PSPath -ErrorAction SilentlyContinue | ForEach-Object { $_.PSPath })
        foreach ($k in $keys) {
            foreach ($t in (Split-Dlls (Get-Value $k 'VerifierDlls'))) {
                if ($t.ToLower() -eq $LoaderDll) { return $true }
            }
        }
    }
    return $false
}

function Install-Loader {
    foreach ($f in $LoaderDll, 'kxgdi.dll') {
        $src = Join-Path $ScriptDir $f
        if (-not (Test-Path $src)) { Fail "$f not found next to the script: $src" }
    }

    # Files first, registry second: a registered but missing provider would
    # stop the program from starting.
    foreach ($f in $LoaderDll, 'kxgdi.dll') {
        try {
            Copy-Item (Join-Path $ScriptDir $f) (Join-Path $System32 $f) -Force
        } catch {
            Fail "Could not copy $f to $System32 (is the game still running?): $_"
        }
        Write-Host "Copied: $(Join-Path $System32 $f)"
    }

    foreach ($exe in $ExePath) {
        Write-Host ''
        Write-Host "Program: $exe"

        $filter = Get-FilterKey $exe
        if ($filter) {
            $key = $filter
            Write-Host '  VxKex settings found for this program: adding kxgdildr.dll after VxKex.' -ForegroundColor Yellow
        } else {
            $key = Join-Path $IfeoRoot ([IO.Path]::GetFileName($exe))
            if (-not (Test-Path $key)) { New-Item $key -Force | Out-Null }

            $other = Get-OtherFilterPaths $exe
            if ($other.Count -gt 0) {
                Write-Host '  Warning: VxKex settings exist for this file name, but for a different path:' -ForegroundColor Yellow
                foreach ($p in $other) { Write-Host "    $p" -ForegroundColor Yellow }
                Write-Host '  If that is the same program, run the script again with exactly that path.' -ForegroundColor Yellow
            }
        }

        $value = Add-LoaderEntry $key
        Write-Host "  VerifierDlls = $value" -ForegroundColor Green
    }

    Write-Host ''
    Write-Host 'Done. If you enable or disable VxKex for these programs later, run this script again.'
    Write-Host 'To watch kxgdi messages, start Sysinternals DebugView (lines starting with "KxGdi").'
}

function Uninstall-Loader {
    foreach ($exe in $ExePath) {
        $base = Join-Path $IfeoRoot ([IO.Path]::GetFileName($exe))
        if (-not (Test-Path $base)) {
            Write-Host "No settings found for: $exe"
            continue
        }

        $removed = $false
        $keys = @($base) + @(Get-ChildItem $base -ErrorAction SilentlyContinue | ForEach-Object { $_.PSPath })
        foreach ($k in $keys) {
            if (Remove-LoaderEntry $k) { $removed = $true }
        }

        # Remove the program's IFEO key if nothing else is left in it.
        $item = Get-Item $base
        if ($item.ValueCount -eq 0 -and $item.SubKeyCount -eq 0) { Remove-Item $base -Force }

        if ($removed) { Write-Host "Loader removed for: $exe" } else { Write-Host "Loader was not registered for: $exe" }
    }

    # Registry first, files second.
    if (Test-LoaderStillUsed) {
        Write-Host ''
        Write-Host 'kxgdildr.dll is still registered for other programs, files in System32 are kept.'
    } else {
        foreach ($f in $LoaderDll, 'kxgdi.dll') {
            $path = Join-Path $System32 $f
            if (Test-Path $path) {
                try {
                    Remove-Item $path -Force
                    Write-Host "Deleted: $path"
                } catch {
                    Write-Host "Could not delete $path (still in use?). Delete it after closing the game." -ForegroundColor Yellow
                }
            }
        }
    }

    Write-Host ''
    Write-Host 'Done.'
}

#
# Method: VxKex (KEX_DllRewriteEntries)
#

$OurEntries = @('gdi32:kxgdi', 'api-ms-win-dx-d3dkmt:kxgdi')
$OurKeys    = @('gdi32', 'api-ms-win-dx-d3dkmt')

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

function Get-VxKexDirs {
    $vxkex = Get-ItemProperty 'HKLM:\SOFTWARE\VXsoft\VxKex' -ErrorAction SilentlyContinue
    if (-not $vxkex -or -not $vxkex.KexDir) {
        Fail 'VxKex is not installed (registry value HKLM\SOFTWARE\VXsoft\VxKex\KexDir is missing). Use -Method Loader instead.'
    }
    return $vxkex.KexDir
}

function Install-VxKex {
    $KexDir  = Get-VxKexDirs
    $Kex64   = Join-Path $KexDir 'Kex64'
    $KexCfg  = Join-Path $KexDir 'KexCfg.exe'
    $DllSrc  = Join-Path $ScriptDir 'kxgdi.dll'
    $DllDest = Join-Path $Kex64 'kxgdi.dll'

    Write-Host "VxKex directory: $KexDir"
    Write-Host 'Note: this method needs VxKex 1.2.0 or later from i486/VxKex. VxKex NEXT ignores it, use -Method Loader there.'

    if (-not (Test-Path $DllSrc)) { Fail "kxgdi.dll not found next to the script: $DllSrc" }
    if (-not (Test-Path $Kex64)) { Fail "Directory not found: $Kex64 (is this a 64-bit Windows with 64-bit VxKex?)" }

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

        $entries = Remove-OurKeys (Split-Entries (Get-Value $key 'KEX_DllRewriteEntries'))
        $entries += $OurEntries
        $value = $entries -join '|'

        Set-ItemProperty $key -Name 'KEX_DllRewriteEntries' -Value $value
        Write-Host "  KEX_DllRewriteEntries = $value" -ForegroundColor Green
    }

    Write-Host ''
    Write-Host 'Done. To watch kxgdi messages, start Sysinternals DebugView (lines starting with "KxGdi:").'
}

function Uninstall-VxKex {
    $KexDir  = Get-VxKexDirs
    $DllDest = Join-Path (Join-Path $KexDir 'Kex64') 'kxgdi.dll'

    foreach ($exe in $ExePath) {
        $key = Get-FilterKey $exe
        if (-not $key) {
            Write-Host "No VxKex settings found for: $exe"
            continue
        }

        $rest = Remove-OurKeys (Split-Entries (Get-Value $key 'KEX_DllRewriteEntries'))

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
}

#
# Main
#

$ExePath = @($ExePath | ForEach-Object { Get-NormalizedPath $_ })

Write-Host "Method: $Method"

if ($Method -eq 'Loader') {
    if ($Uninstall) { Uninstall-Loader } else { Install-Loader }
} else {
    if ($Uninstall) { Uninstall-VxKex } else { Install-VxKex }
}

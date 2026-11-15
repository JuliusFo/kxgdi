<#
.SYNOPSIS
    Checks that Install-KxGdi.ps1 reads its program paths correctly however it
    is started. Changes nothing (uses -ListPrograms), needs no administrator
    rights. Compatible with PowerShell 2.0 (Windows 7).

.DESCRIPTION
    Issue #1: "powershell -File .\Install-KxGdi.ps1 -ExePath "a","b"" bound
    the second path to -Method. This runs the installer in a separate
    powershell.exe the ways users start it (from PowerShell, from cmd.exe,
    as a direct call) and compares the "Programs:" list it prints.

    Run it with Windows PowerShell (powershell.exe), not pwsh: how arguments
    reach powershell.exe -File depends on the calling shell, and users call it
    from Windows PowerShell or cmd.exe.

    Exit code = number of failed checks.
#>
param(
    # Path of Install-KxGdi.ps1 (default: ..\install\Install-KxGdi.ps1 or next to this script).
    [string] $Installer
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Installer) {
    $Installer = Join-Path (Join-Path (Split-Path -Parent $here) 'install') 'Install-KxGdi.ps1'
    if (-not (Test-Path $Installer)) { $Installer = Join-Path $here 'Install-KxGdi.ps1' }
}
$Installer = [IO.Path]::GetFullPath($Installer)

$a = 'C:\Program Files (x86)\Grinding Gear Games\Path of Exile\PathOfExile.exe'
$b = 'C:\Users\Test User\AppData\Local\Temp\PathOfExile.tmp'
$c = 'D:\Games\PathOfExileSteam.exe'

$tmp = Join-Path ([IO.Path]::GetTempPath()) ('kxgdi-argtest-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $tmp | Out-Null
$withComma = Join-Path $tmp 'Path of Exile, copy.exe'
Set-Content -LiteralPath $withComma -Value ''

$failed = 0

function Get-Programs([string[]] $Output) {
    $list = @()
    $on = $false
    foreach ($line in $Output) {
        if ($line -eq 'Programs:') { $on = $true; continue }
        if ($on) {
            if ($line.StartsWith('  ')) { $list += $line.Substring(2) } else { break }
        }
    }
    return ,$list
}

function Check([string] $Name, [string[]] $Output, [int] $ExitCode, [string[]] $Expected) {
    $got = Get-Programs $Output
    $ok = ($ExitCode -eq 0) -and ($got.Count -eq $Expected.Count)
    if ($ok) { for ($i = 0; $i -lt $got.Count; $i++) { if ($got[$i] -cne $Expected[$i]) { $ok = $false } } }
    if ($ok) {
        Write-Host "[ OK ] $Name"
    } else {
        Write-Host "[FAIL] $Name (exit code $ExitCode)" -ForegroundColor Red
        Write-Host "       expected: $($Expected -join ' | ')"
        Write-Host "       got:      $($got -join ' | ')"
        foreach ($line in $Output) { Write-Host "       > $line" }
        $script:failed++
    }
}

function Invoke-Cmd([string] $Line) {
    # A real batch file, so the quoting is exactly what a user types into cmd.exe.
    $bat = Join-Path $tmp 'call.cmd'
    Set-Content -LiteralPath $bat -Value "@$Line" -Encoding Default
    $out = @(& cmd.exe /c "`"$bat`"" 2>&1 | ForEach-Object { "$_" })
    return @{ Out = $out; Code = $LASTEXITCODE }
}

try {
    $ps = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $Installer)

    # From PowerShell, exactly as in the README of v0.2.0 (issue #1)
    $out = @(& powershell.exe $ps -ExePath "$a","$b" -ListPrograms 2>&1 | ForEach-Object { "$_" })
    Check 'powershell -File, two paths, from PowerShell' $out $LASTEXITCODE @($a, $b)

    $out = @(& powershell.exe $ps -ExePath "$a", "$b" -Uninstall -ListPrograms 2>&1 | ForEach-Object { "$_" })
    Check 'powershell -File, two paths and -Uninstall, from PowerShell' $out $LASTEXITCODE @($a, $b)

    $out = @(& powershell.exe $ps -Method Loader -ExePath "$a","$b","$c" -ListPrograms 2>&1 | ForEach-Object { "$_" })
    Check 'powershell -File, -Method first, three paths, from PowerShell' $out $LASTEXITCODE @($a, $b, $c)

    # From cmd.exe
    $r = Invoke-Cmd "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$Installer`" -ExePath `"$a`",`"$b`" -ListPrograms"
    Check 'powershell -File, two paths, from cmd.exe' $r.Out $r.Code @($a, $b)

    $r = Invoke-Cmd "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$Installer`" -ExePath `"$a`", `"$b`" -ListPrograms"
    Check 'powershell -File, two paths with space after the comma, from cmd.exe' $r.Out $r.Code @($a, $b)

    # Direct call (README since v0.2.1)
    $cmd = "& '$Installer' -ExePath '$a','$b' -ListPrograms"
    $out = @(& powershell.exe -NoProfile -ExecutionPolicy Bypass -Command $cmd 2>&1 | ForEach-Object { "$_" })
    Check 'direct call with array' $out $LASTEXITCODE @($a, $b)

    $cmd = "& '$Installer' '$a' '$b' -ListPrograms"
    $out = @(& powershell.exe -NoProfile -ExecutionPolicy Bypass -Command $cmd 2>&1 | ForEach-Object { "$_" })
    Check 'direct call, paths without -ExePath' $out $LASTEXITCODE @($a, $b)

    # An existing file with a comma in its name stays one path
    $out = @(& powershell.exe $ps -ExePath "$withComma" -ListPrograms 2>&1 | ForEach-Object { "$_" })
    Check 'existing file with a comma in its name' $out $LASTEXITCODE @($withComma)

    # Duplicates are removed (case-insensitive)
    $out = @(& powershell.exe $ps -ExePath "$a","$($a.ToUpper())" -ListPrograms 2>&1 | ForEach-Object { "$_" })
    Check 'duplicate path' $out $LASTEXITCODE @($a)

    # A mistyped parameter must not end up as a program path
    $out = @(& powershell.exe $ps -ExePath "$a" -Methode VxKex -ListPrograms 2>&1 | ForEach-Object { "$_" })
    if ($LASTEXITCODE -eq 1 -and (($out -join "`n") -match "Unknown parameter '-Methode'")) {
        Write-Host '[ OK ] mistyped parameter is rejected'
    } else {
        Write-Host "[FAIL] mistyped parameter is rejected (exit code $LASTEXITCODE)" -ForegroundColor Red
        foreach ($line in $out) { Write-Host "       > $line" }
        $failed++
    }
} finally {
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ''
if ($failed -eq 0) { Write-Host 'RESULT: all installer argument checks passed' } else { Write-Host "RESULT: $failed check(s) failed" -ForegroundColor Red }
exit $failed

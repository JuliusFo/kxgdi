<#
.SYNOPSIS
    Setup window for kxgdi: run the tests, find Path of Exile, install and
    uninstall without typing commands. Compatible with PowerShell 2.0
    (Windows 7).

.DESCRIPTION
    Start it with Setup.cmd (double-click). The window asks for administrator
    rights itself. It does not change anything on its own: every action runs
    one of the scripts next to it and shows their output.

      Run tests     kxgdi-test.exe and Test-Loader.ps1
      Find game     Steam libraries, installed programs, default folders,
                    running PathOfExile processes and programs kxgdi is already
                    registered for; adds PathOfExile.tmp by name
      Install /     Install-KxGdi.ps1 (-Method Loader) with the listed programs
      Uninstall

    For -Method VxKex and other options use Install-KxGdi.ps1 directly.
#>
param(
    # Development and CI: check the logic without showing the window and
    # without administrator rights. Exit code = number of failed checks.
    [switch] $SelfTest,
    # Development: render the window with sample content to this PNG file.
    [string] $Snapshot,
    # Internal: set when the script restarted itself (elevated, STA, 64-bit).
    [switch] $Relaunched
)

$ErrorActionPreference = 'Stop'

$ScriptPath    = $MyInvocation.MyCommand.Path
$Here          = Split-Path -Parent $ScriptPath
$PowerShellExe = Join-Path $PSHOME 'powershell.exe'
$IssuesUrl     = 'https://github.com/JuliusFo/kxgdi/issues'

[void] [Reflection.Assembly]::LoadWithPartialName('System.Windows.Forms')
[void] [Reflection.Assembly]::LoadWithPartialName('System.Drawing')

function Show-Message([string] $Text, [string] $Icon = 'Information') {
    [void] [Windows.Forms.MessageBox]::Show($Text, 'kxgdi Setup', 'OK', $Icon)
}

function Test-Admin {
    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

#
# Restart elevated, in STA mode (needed for the file dialog and the clipboard),
# as 64-bit process and with a hidden console. Always restarts once, so the
# console window of Setup.cmd closes; Setup.cmd shows errors of this first
# instance (e.g. a blocked execution policy).
#

if (-not $SelfTest -and -not $Snapshot) {
    $isSta = ([Threading.Thread]::CurrentThread.GetApartmentState() -eq 'STA')
    $is64  = ([IntPtr]::Size -eq 8)
    $admin = Test-Admin

    if ($Relaunched -and -not ($admin -and $isSta -and $is64)) {
        Show-Message 'kxgdi Setup could not start with administrator rights as a 64-bit program.' 'Warning'
        exit 1
    }

    if (-not $Relaunched) {
        # The elevated process does not see network drives mapped by the user.
        $drive = [IO.Path]::GetPathRoot($ScriptPath)
        $onNetworkDrive = $false
        try { if ($drive -match '^[A-Za-z]:\\$') { $onNetworkDrive = ((New-Object IO.DriveInfo $drive).DriveType -eq 'Network') } } catch { }
        if ($onNetworkDrive -and -not $admin) {
            Show-Message "kxgdi is on a network drive ($drive), which the setup cannot use with administrator rights.`r`n`r`nPlease copy the kxgdi folder to a local disk, e.g. C:\kxgdi, and start Setup.cmd there." 'Warning'
            exit 0
        }

        $exe = $PowerShellExe
        if (-not $is64) {
            $exe = Join-Path $env:windir 'Sysnative\WindowsPowerShell\v1.0\powershell.exe'
            if (-not (Test-Path $exe)) {
                Show-Message 'kxgdi needs a 64-bit Windows 7.' 'Warning'
                exit 1
            }
        }

        $argLine = '-NoProfile -ExecutionPolicy Bypass -STA -WindowStyle Hidden -File "' + $ScriptPath + '" -Relaunched'
        try {
            if ($admin) { Start-Process $exe -ArgumentList $argLine }
            else        { Start-Process $exe -ArgumentList $argLine -Verb RunAs }
        } catch {
            Show-Message ("kxgdi Setup needs administrator rights to install the loader.`r`n`r`n" + $_) 'Warning'
        }
        exit 0
    }
}

#
# Output
#

$script:ui  = @{}
$script:log = @()

function Write-Out([string] $Text) {
    if ($script:ui.Output) {
        $script:ui.Output.AppendText($Text + "`r`n")
    } else {
        $script:log += $Text
    }
}

# Runs a program and shows its output line by line. Returns the exit code.
function Invoke-Logged([string] $File, [string] $Arguments) {
    if (-not (Test-Path -LiteralPath $File)) {
        Write-Out "Not found: $File"
        return -1
    }

    $psi = New-Object Diagnostics.ProcessStartInfo
    $psi.FileName               = $File
    $psi.Arguments              = $Arguments
    $psi.WorkingDirectory       = $Here
    $psi.UseShellExecute        = $false
    $psi.CreateNoWindow         = $true
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError  = $true

    $p = [Diagnostics.Process]::Start($psi)
    while ($true) {
        $line = $p.StandardOutput.ReadLine()
        if ($line -eq $null) { break }
        Write-Out $line
        [Windows.Forms.Application]::DoEvents()
    }
    $err = $p.StandardError.ReadToEnd()
    $p.WaitForExit()
    if ($err -and $err.Trim()) { Write-Out $err.TrimEnd() }
    return $p.ExitCode
}

#
# Finding Path of Exile
#

# Library folders from Steam's libraryfolders.vdf (current and old format).
function Get-SteamLibraries([string] $VdfText) {
    $result = @()
    if (-not $VdfText) { return ,$result }
    foreach ($line in ($VdfText -split "`r?`n")) {
        if ($line -match '^\s*"(?:path|\d+)"\s+"(.+)"\s*$') {
            $p = $Matches[1].Replace('\\', '\')
            if ($p.Contains(':')) { $result += $p }
        }
    }
    return ,$result
}

# "Path of Exile*" folders in Steam libraries. A library on a missing drive or
# with a malformed path is skipped.
function Get-LibraryGameFolders([string[]] $Libs) {
    $result = @()
    foreach ($lib in $Libs) {
        try {
            $common = [IO.Path]::Combine($lib, 'steamapps\common')
            foreach ($d in @(Get-ChildItem -LiteralPath $common -ErrorAction SilentlyContinue)) {
                if ($d.PSIsContainer -and $d.Name -like 'Path of Exile*') { $result += $d.FullName }
            }
        } catch { }
    }
    return ,$result
}

function Get-GameFolders {
    $folders = @()

    # Installed programs (the standalone client and Steam both register here)
    $roots = @(
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall',
        'HKLM:\SOFTWARE\Wow6432Node\Microsoft\Windows\CurrentVersion\Uninstall',
        'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall'
    )
    foreach ($root in $roots) {
        foreach ($key in @(Get-ChildItem $root -ErrorAction SilentlyContinue)) {
            $p = Get-ItemProperty $key.PSPath -ErrorAction SilentlyContinue
            if ($p -and $p.DisplayName -like 'Path of Exile*' -and $p.InstallLocation) {
                $folders += ([string] $p.InstallLocation).Trim('"')
            }
        }
    }

    # Steam libraries. HKCU belongs to the account that confirmed the
    # administrator prompt, which may not be the player's, so also HKLM.
    $libs = @()
    $steamPaths = @()
    $steam = Get-ItemProperty 'HKCU:\Software\Valve\Steam' -ErrorAction SilentlyContinue
    if ($steam -and $steam.SteamPath) { $steamPaths += ([string] $steam.SteamPath).Replace('/', '\') }
    foreach ($k in 'HKLM:\SOFTWARE\Wow6432Node\Valve\Steam', 'HKLM:\SOFTWARE\Valve\Steam') {
        $steam = Get-ItemProperty $k -ErrorAction SilentlyContinue
        if ($steam -and $steam.InstallPath) { $steamPaths += [string] $steam.InstallPath }
    }
    # Paths from the registry and libraryfolders.vdf may point to drives that
    # no longer exist (an unplugged library disk) or be malformed. Join-Path
    # throws for a missing drive, so use [IO.Path]::Combine, and let one bad
    # entry never stop the search.
    foreach ($steamPath in $steamPaths) {
        $libs += $steamPath
        try {
            $vdf = [IO.Path]::Combine($steamPath, 'steamapps\libraryfolders.vdf')
            if (Test-Path -LiteralPath $vdf) { $libs += Get-SteamLibraries ([IO.File]::ReadAllText($vdf)) }
        } catch { }
    }
    $folders += Get-LibraryGameFolders $libs

    # Default folders of the standalone client
    foreach ($pf in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
        if (-not $pf) { continue }
        try {
            $ggg = [IO.Path]::Combine($pf, 'Grinding Gear Games')
            foreach ($d in @(Get-ChildItem -LiteralPath $ggg -ErrorAction SilentlyContinue)) {
                if ($d.PSIsContainer -and $d.Name -like 'Path of Exile*') { $folders += $d.FullName }
            }
        } catch { }
    }

    $result = @()
    foreach ($f in $folders) {
        try {
            if (-not $f -or -not (Test-Path -LiteralPath $f)) { continue }
            $f = [IO.Path]::GetFullPath($f)
            # "D:\" must stay "D:\" ("D:" would mean the current folder of D:)
            if ($f.Length -gt [IO.Path]::GetPathRoot($f).Length) { $f = $f.TrimEnd('\') }
            if (-not ($result -contains $f)) { $result += $f }
        } catch { }
    }
    return ,$result
}

function Get-GameExes([string] $Folder) {
    $result = @()
    foreach ($f in @(Get-ChildItem -LiteralPath $Folder -Filter 'PathOfExile*.exe' -ErrorAction SilentlyContinue)) {
        if (-not $f.PSIsContainer) { $result += $f.FullName }
    }
    return ,$result
}

# Running PathOfExile processes, e.g. PathOfExile.tmp while its error message is open.
function Get-RunningGames {
    $result = @()
    foreach ($p in @(Get-Process -ErrorAction SilentlyContinue)) {
        if ($p.Name -notlike 'PathOfExile*') { continue }
        try { if ($p.Path) { $result += $p.Path } } catch { }
    }
    return ,$result
}

function Add-Unique([object[]] $List, [string] $Item) {
    foreach ($x in $List) { if ($x -eq $Item) { return ,$List } }   # -eq ignores case
    return ,($List + $Item)
}

# The game also starts a PathOfExile.tmp (the error message of issue #1 named
# it). The loader is registered per file name, so the name alone is enough and
# its folder does not matter. Added whenever a PathOfExile program is listed;
# a full path of PathOfExile.tmp (found as running process) becomes the name.
function Add-GameTmp([object[]] $List) {
    $result = @()
    $hasGame = $false
    $hasTmp = $false
    foreach ($x in $List) {
        $name = [IO.Path]::GetFileName([string] $x)
        if ($name -eq 'PathOfExile.tmp') {
            if (-not $hasTmp) { $result += 'PathOfExile.tmp'; $hasTmp = $true }
            continue
        }
        if ($name -like 'PathOfExile*') { $hasGame = $true }
        $result += $x
    }
    if ($hasGame -and -not $hasTmp) { $result += 'PathOfExile.tmp' }
    return ,$result
}

# Programs for which kxgdildr.dll is registered by file name (in the IFEO key
# of that name, not only in a VxKex filter subkey for one path: listing those
# by name would widen them to every folder on the next install). Lets the
# window uninstall kxgdi even after the game itself was removed. The loader
# test program is left out; Test-Loader.ps1 cleans it up itself.
function Get-RegisteredPrograms([string] $Root = 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Image File Execution Options') {
    $result = @()
    foreach ($key in @(Get-ChildItem $Root -ErrorAction SilentlyContinue)) {
        if ($key.PSChildName -eq 'kxgdi-ldrtest.exe') { continue }
        $p = Get-ItemProperty $key.PSPath -ErrorAction SilentlyContinue
        if ($p -and $p.VerifierDlls -and ((' ' + ([string] $p.VerifierDlls).ToLower() + ' ') -match '\skxgdildr\.dll\s')) {
            $result = Add-Unique $result $key.PSChildName
        }
    }
    return ,$result
}

function Find-Programs {
    # Each source on its own: a failure in one must not hide the others.
    $folders = @(); $running = @(); $registered = @()
    try { $folders    = Get-GameFolders }       catch { Write-Out "(Searching game folders failed: $_)" }
    try { $running    = Get-RunningGames }      catch { Write-Out "(Searching running programs failed: $_)" }
    try { $registered = Get-RegisteredPrograms } catch { Write-Out "(Reading the kxgdi registrations failed: $_)" }

    $result = @()
    foreach ($folder in $folders) {
        foreach ($exe in (Get-GameExes $folder)) { $result = Add-Unique $result $exe }
    }
    foreach ($exe in $running) { $result = Add-Unique $result $exe }
    $result = Add-GameTmp $result
    # Already registered by name; skip names that are listed with a full path.
    foreach ($name in $registered) {
        $listed = $false
        foreach ($x in $result) { if ([IO.Path]::GetFileName([string] $x) -eq $name) { $listed = $true } }
        if (-not $listed) { $result += $name }
    }
    return ,$result
}

# Reads a text file that another process still has open for writing (the
# loader keeps its log open, sharing it for reading only, until the program
# exits). Detects the UTF-16 byte order mark of the loader log.
function Read-SharedLines([string] $Path) {
    $stream = New-Object IO.FileStream -ArgumentList $Path, ([IO.FileMode]::Open), ([IO.FileAccess]::Read), ([IO.FileShare]::ReadWrite)
    try {
        $reader = New-Object IO.StreamReader -ArgumentList $stream, $true
        $text = $reader.ReadToEnd()
    } finally {
        $stream.Close()
    }
    return ,($text -split "`r?`n")
}

#
# Actions
#

function Get-InstallerArguments([string[]] $Programs, [switch] $Uninstall, [switch] $ListOnly) {
    $s = '-NoProfile -ExecutionPolicy Bypass -File "' + (Join-Path $Here 'Install-KxGdi.ps1') + '" -ExePath'
    foreach ($p in $Programs) { $s += ' "' + $p + '"' }
    if ($Uninstall) { $s += ' -Uninstall' }
    if ($ListOnly)  { $s += ' -ListPrograms' }
    return $s
}

function Get-ListedPrograms {
    $result = @()
    foreach ($item in $script:ui.List.Items) { $result += [string] $item }
    return ,$result
}

function Set-Busy([bool] $Busy) {
    $script:busy = $Busy
    foreach ($b in $script:ui.Buttons) { $b.Enabled = -not $Busy }
    if ($Busy) { $script:ui.Form.Cursor = [Windows.Forms.Cursors]::WaitCursor }
    else       { $script:ui.Form.Cursor = [Windows.Forms.Cursors]::Default }
    [Windows.Forms.Application]::DoEvents()
}

function Invoke-Action([scriptblock] $Action) {
    Set-Busy $true
    try { & $Action } catch { Write-Out "ERROR: $_" } finally { Set-Busy $false }
}

function Start-Tests {
    Write-Out ''
    Write-Out '=== Self-test of kxgdi (kxgdi-test.exe) ==='
    $c1 = Invoke-Logged (Join-Path $Here 'kxgdi-test.exe') ''
    Write-Out ''
    Write-Out '=== Loader test (Test-Loader.ps1) ==='
    $c2 = Invoke-Logged $PowerShellExe ('-NoProfile -ExecutionPolicy Bypass -File "' + (Join-Path $Here 'Test-Loader.ps1') + '"')
    Write-Out ''
    if ($c1 -eq 0 -and $c2 -eq 0) {
        Write-Out '>>> Both tests passed. Continue with step 2.'
    } else {
        Write-Out '>>> A test FAILED. Please do not install. Click "Copy output" and post it in a new issue on GitHub.'
    }
}

# Adds programs to the list (no duplicates), plus PathOfExile.tmp when needed.
# Returns the number of new entries.
function Add-ListItems([object[]] $Programs) {
    $added = 0
    # Only the new programs: an entry the user removed stays removed.
    foreach ($p in (Add-GameTmp @($Programs))) {
        $known = $false
        foreach ($item in $script:ui.List.Items) { if ([string] $item -eq $p) { $known = $true } }
        if (-not $known) { [void] $script:ui.List.Items.Add($p); $added++ }
    }
    return $added
}

function Find-AndAdd {
    Write-Out ''
    Write-Out '=== Looking for Path of Exile ==='
    $found = Find-Programs
    foreach ($p in $found) { Write-Out "  $p" }
    $added = Add-ListItems $found
    if ($found.Count -eq 0) {
        Write-Out 'Path of Exile was not found. Click "Add file..." and select PathOfExile.exe or PathOfExileSteam.exe.'
        Write-Out 'Tip: start the game; while its error message is open, click "Find game" again.'
    } else {
        Write-Out "Found $($found.Count) program(s), $added new in the list. Remove the ones you do not use."
    }
}

function Add-Files {
    $dlg = New-Object Windows.Forms.OpenFileDialog
    $dlg.Title       = 'Select the game (PathOfExile.exe, PathOfExileSteam.exe, ...)'
    $dlg.Filter      = 'Programs (*.exe;*.tmp)|*.exe;*.tmp|All files (*.*)|*.*'
    $dlg.Multiselect = $true
    if ($dlg.ShowDialog() -eq [Windows.Forms.DialogResult]::OK) {
        [void] (Add-ListItems $dlg.FileNames)
    }
}

function Remove-Selected {
    $selected = @()
    foreach ($item in $script:ui.List.SelectedItems) { $selected += $item }
    foreach ($item in $selected) { $script:ui.List.Items.Remove($item) }
}

function Start-Installer([bool] $Uninstall) {
    $programs = Get-ListedPrograms
    if ($programs.Count -eq 0) {
        Show-Message 'Add the game in step 2 first ("Find game" or "Add file...").' 'Warning'
        return
    }
    Write-Out ''
    if ($Uninstall) {
        Write-Out '=== Uninstall ==='
        $code = Invoke-Logged $PowerShellExe (Get-InstallerArguments $programs -Uninstall)
    } else {
        Write-Out '=== Install ==='
        $code = Invoke-Logged $PowerShellExe (Get-InstallerArguments $programs)
    }
    Write-Out ''
    if ($code -ne 0) {
        Write-Out '>>> FAILED, see the messages above.'
    } elseif ($Uninstall) {
        Write-Out '>>> Uninstall finished. The messages above show for which programs kxgdi was removed.'
    } else {
        Write-Out '>>> Installed. Start the game now. If a new "entry point ... not found" message appears,'
        Write-Out '    or the game crashes, click "Show loader logs", then "Copy output", and open an issue.'
    }
}

function Show-LoaderLogs {
    Write-Out ''
    Write-Out '=== Loader logs ==='
    $logs = @(Get-ChildItem (Join-Path $env:windir 'Temp') -Filter 'kxgdildr-*.log' -ErrorAction SilentlyContinue)
    if ($logs.Count -eq 0) {
        Write-Out 'No loader logs yet. They are written when a program starts with kxgdi installed.'
        return
    }
    foreach ($f in $logs) {
        Write-Out "--- $($f.FullName) ($($f.LastWriteTime)) ---"
        try {
            foreach ($line in (Read-SharedLines $f.FullName)) { Write-Out $line }
        } catch {
            Write-Out "(could not read: $_)"
        }
    }
}

function Copy-Output {
    $text = $script:ui.Output.Text
    if ($text) {
        [Windows.Forms.Clipboard]::SetText($text)
        Write-Out '(Output copied to the clipboard.)'
    }
}

function Open-Issues {
    # Through Explorer, so the browser does not run with administrator rights.
    try { Start-Process 'explorer.exe' $IssuesUrl } catch { Start-Process $IssuesUrl }
}

#
# Window
#

function New-Label([string] $Text, [int] $MaxWidth = 600) {
    $l = New-Object Windows.Forms.Label
    $l.Text = $Text
    $l.AutoSize = $true
    $l.MaximumSize = New-Object Drawing.Size -ArgumentList $MaxWidth, 0
    $l.Margin = New-Object Windows.Forms.Padding -ArgumentList 3, 3, 3, 6
    return $l
}

function New-Button([string] $Text, [scriptblock] $OnClick) {
    $b = New-Object Windows.Forms.Button
    $b.Text = $Text
    $b.AutoSize = $true
    $b.Padding = New-Object Windows.Forms.Padding -ArgumentList 6, 2, 6, 2
    $b.Add_Click($OnClick)
    $script:ui.Buttons += $b
    return $b
}

function New-Group([string] $Text) {
    $g = New-Object Windows.Forms.GroupBox
    $g.Text = $Text
    $g.AutoSize = $true
    $g.AutoSizeMode = 'GrowAndShrink'
    $g.Dock = 'Fill'
    $g.Padding = New-Object Windows.Forms.Padding -ArgumentList 8, 6, 8, 6
    $g.Margin = New-Object Windows.Forms.Padding -ArgumentList 0, 4, 0, 4
    return $g
}

function New-Flow {
    $f = New-Object Windows.Forms.FlowLayoutPanel
    $f.AutoSize = $true
    $f.AutoSizeMode = 'GrowAndShrink'
    $f.Dock = 'Fill'
    $f.WrapContents = $true
    return $f
}

function Get-WindowsStatus {
    $v = [Environment]::OSVersion.Version
    if ($v.Major -eq 6 -and $v.Minor -eq 1) {
        $sp = [Environment]::OSVersion.ServicePack
        if (-not $sp) { return @{ Text = 'Windows 7 without Service Pack 1. kxgdi needs Windows 7 SP1: please install SP1 (Windows Update) first.'; Ok = $false } }
        return @{ Text = "Windows 7 64-bit $sp - OK."; Ok = $true }
    }
    return @{ Text = "This is Windows $($v.Major).$($v.Minor), not Windows 7. kxgdi is only needed on Windows 7; installing is refused here."; Ok = $false }
}

function New-SetupForm {
    $script:ui = @{ Buttons = @() }

    $form = New-Object Windows.Forms.Form
    $form.Text = 'kxgdi Setup'
    $form.Font = [Drawing.SystemFonts]::MessageBoxFont
    $form.StartPosition = 'CenterScreen'
    $form.ClientSize = New-Object Drawing.Size -ArgumentList 700, 760
    $form.MinimumSize = New-Object Drawing.Size -ArgumentList 560, 600
    $script:ui.Form = $form
    $script:busy = $false

    # While a step runs, the window must stay open: otherwise it vanishes but
    # the step keeps running, and a second setup could run at the same time.
    $form.Add_FormClosing({
        if ($script:busy) {
            $_.Cancel = $true
            [void] [Windows.Forms.MessageBox]::Show('Please wait until the current step has finished.', 'kxgdi Setup', 'OK', 'Information')
        }
    })

    $root = New-Object Windows.Forms.TableLayoutPanel
    $root.Dock = 'Fill'
    $root.ColumnCount = 1
    $root.Padding = New-Object Windows.Forms.Padding -ArgumentList 10
    [void] $root.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle -ArgumentList ([Windows.Forms.SizeType]::Percent), 100))
    $form.Controls.Add($root)

    # Header
    $title = New-Label 'kxgdi - run Path of Exile on Windows 7'
    $title.Font = New-Object Drawing.Font -ArgumentList $form.Font.FontFamily, ($form.Font.Size + 3), ([Drawing.FontStyle]::Bold)
    $root.Controls.Add($title)

    $status = Get-WindowsStatus
    $statusLabel = New-Label $status.Text
    if ($status.Ok) { $statusLabel.ForeColor = [Drawing.Color]::FromArgb(0, 120, 0) }
    else            { $statusLabel.ForeColor = [Drawing.Color]::FromArgb(176, 80, 0) }
    $root.Controls.Add($statusLabel)
    $script:ui.Status = $statusLabel

    # Step 1
    $g1 = New-Group 'Step 1: Test'
    $f1 = New-Flow
    $f1.FlowDirection = 'TopDown'
    $f1.Controls.Add((New-Label 'Checks that kxgdi works on this PC. Nothing stays installed. Takes a few seconds.'))
    $f1.Controls.Add((New-Button 'Run tests' { Invoke-Action { Start-Tests } }))
    $g1.Controls.Add($f1)
    $root.Controls.Add($g1)

    # Step 2
    $g2 = New-Group 'Step 2: Programs'
    $t2 = New-Object Windows.Forms.TableLayoutPanel
    $t2.Dock = 'Fill'
    $t2.AutoSize = $true
    $t2.ColumnCount = 2
    [void] $t2.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle -ArgumentList ([Windows.Forms.SizeType]::Percent), 100))
    [void] $t2.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle -ArgumentList ([Windows.Forms.SizeType]::AutoSize)))

    $list = New-Object Windows.Forms.ListBox
    $list.Dock = 'Fill'
    $list.Height = 100
    $list.SelectionMode = 'MultiExtended'
    $list.HorizontalScrollbar = $true
    $list.IntegralHeight = $false
    $script:ui.List = $list
    $t2.Controls.Add($list, 0, 0)

    $side = New-Flow
    $side.FlowDirection = 'TopDown'
    $side.WrapContents = $false
    $side.Controls.Add((New-Button 'Find game' { Invoke-Action { Find-AndAdd } }))
    $side.Controls.Add((New-Button 'Add file...' { Invoke-Action { Add-Files } }))
    $side.Controls.Add((New-Button 'Remove' { Invoke-Action { Remove-Selected } }))
    $t2.Controls.Add($side, 1, 0)

    $note = New-Label 'kxgdi is set up only for the programs in this list. The game also starts a PathOfExile.tmp; it is added by name, so its folder does not matter.'
    $t2.Controls.Add($note, 0, 1)
    $t2.SetColumnSpan($note, 2)
    $g2.Controls.Add($t2)
    $root.Controls.Add($g2)

    # Step 3
    $g3 = New-Group 'Step 3: Install'
    $f3 = New-Flow
    $f3.Controls.Add((New-Button 'Install' { Invoke-Action { Start-Installer $false } }))
    $f3.Controls.Add((New-Button 'Uninstall' { Invoke-Action { Start-Installer $true } }))
    $f3.Controls.Add((New-Button 'Show loader logs' { Invoke-Action { Show-LoaderLogs } }))
    $g3.Controls.Add($f3)
    $root.Controls.Add($g3)

    # Output
    $out = New-Object Windows.Forms.TextBox
    $out.Multiline = $true
    $out.ReadOnly = $true
    $out.ScrollBars = 'Both'
    $out.WordWrap = $false
    $out.BackColor = [Drawing.SystemColors]::Window
    $out.Font = New-Object Drawing.Font -ArgumentList 'Consolas', 9
    $out.Dock = 'Fill'
    $out.Margin = New-Object Windows.Forms.Padding -ArgumentList 0, 6, 0, 6
    $root.Controls.Add($out)
    $script:ui.Output = $out

    # Footer
    $f4 = New-Flow
    $f4.Controls.Add((New-Button 'Copy output' { Invoke-Action { Copy-Output } }))
    $link = New-Object Windows.Forms.LinkLabel
    $link.Text = 'Report a problem (GitHub issues)'
    $link.AutoSize = $true
    $link.Margin = New-Object Windows.Forms.Padding -ArgumentList 12, 9, 3, 3
    $link.Add_LinkClicked({ Open-Issues })
    $f4.Controls.Add($link)
    $root.Controls.Add($f4)

    # Row sizes: everything as tall as needed, the output gets the rest.
    for ($i = 0; $i -lt $root.Controls.Count; $i++) {
        if ($root.Controls[$i] -eq $out) {
            [void] $root.RowStyles.Add((New-Object Windows.Forms.RowStyle -ArgumentList ([Windows.Forms.SizeType]::Percent), 100))
        } else {
            [void] $root.RowStyles.Add((New-Object Windows.Forms.RowStyle -ArgumentList ([Windows.Forms.SizeType]::AutoSize)))
        }
    }
    $root.RowCount = $root.Controls.Count

    return $form
}

#
# Self-test (development and CI)
#

if ($SelfTest) {
    $failed = 0
    function Check([string] $Name, [bool] $Ok, [string] $Detail = '') {
        if ($Ok) { Write-Host "[ OK ] $Name" }
        else     { Write-Host "[FAIL] $Name $Detail" -ForegroundColor Red; $script:failed++ }
    }

    $vdfNew = "`"libraryfolders`"`r`n{`r`n`t`"0`"`r`n`t{`r`n`t`t`"path`"`t`t`"C:\\Program Files (x86)\\Steam`"`r`n`t`t`"label`"`t`t`"`"`r`n`t}`r`n`t`"1`"`r`n`t{`r`n`t`t`"path`"`t`t`"D:\\SteamLibrary`"`r`n`t}`r`n}"
    $libs = Get-SteamLibraries $vdfNew
    Check 'Steam libraries (current format)' (($libs -join '|') -eq 'C:\Program Files (x86)\Steam|D:\SteamLibrary') ($libs -join '|')

    $vdfOld = "`"LibraryFolders`"`n{`n`t`"TimeNextStatsReport`"`t`t`"1612345678`"`n`t`"ContentStatsID`"`t`t`"-123`"`n`t`"1`"`t`t`"E:\\Games\\Steam`"`n}"
    $libs = Get-SteamLibraries $vdfOld
    Check 'Steam libraries (old format)' (($libs -join '|') -eq 'E:\Games\Steam') ($libs -join '|')

    $tmp = Join-Path ([IO.Path]::GetTempPath()) ('kxgdi-setuptest-' + [Guid]::NewGuid().ToString('N'))
    $game = Join-Path $tmp 'Path of Exile'
    New-Item -ItemType Directory $game | Out-Null
    try {
        foreach ($n in 'PathOfExileSteam.exe', 'PathOfExile_x64.exe', 'Client.txt', 'Other.exe') {
            Set-Content -LiteralPath (Join-Path $game $n) -Value ''
        }
        $exes = Get-GameExes $game
        $names = @($exes | ForEach-Object { [IO.Path]::GetFileName($_) } | Sort-Object)
        Check 'game executables in a folder' (($names -join '|') -eq 'PathOfExile_x64.exe|PathOfExileSteam.exe') ($names -join '|')

        # A Steam library on a drive that does not exist (unplugged disk) is skipped
        $missing = $null
        foreach ($c in [char[]] 'QRSTUVWXYZJKLMNOP') { if (-not (Test-Path -LiteralPath "$($c):\")) { $missing = "$($c):\SteamLibrary"; break } }
        New-Item -ItemType Directory (Join-Path $tmp 'Lib\steamapps\common\Path of Exile') | Out-Null
        $found = @()
        $ok = $true
        try { $found = Get-LibraryGameFolders @($missing, 'bad|path', (Join-Path $tmp 'Lib')) } catch { $ok = $false; Write-Host "       $_" }
        Check "Steam library on a missing drive ($missing) is skipped" ($ok -and $found.Count -eq 1 -and $found[0] -like '*\Lib\steamapps\common\Path of Exile') ($found -join '|')

        $l = Add-Unique @('C:\A\x.exe') 'c:\a\X.EXE'
        Check 'duplicates are ignored (case-insensitive)' ($l.Count -eq 1)

        $ok = $true
        try { [void] (Find-Programs) } catch { $ok = $false; Write-Host "       $_" }
        Check 'searching this PC runs without errors' $ok

        # The installer reads exactly the programs of the list
        $programs = @((Join-Path $game 'PathOfExileSteam.exe'), 'PathOfExile.tmp')
        $script:log = @()
        $code = Invoke-Logged $PowerShellExe (Get-InstallerArguments $programs -ListOnly)
        $listed = @()
        $on = $false
        foreach ($line in $script:log) {
            if ($line -eq 'Programs:') { $on = $true; continue }
            if ($on) { if ($line.StartsWith('  ')) { $listed += $line.Substring(2) } else { break } }
        }
        Check 'installer receives the listed programs' ($code -eq 0 -and ($listed -join '|') -ceq ($programs -join '|')) ("(exit code $code) " + ($script:log -join ' / '))

        $uninstallArgs = Get-InstallerArguments $programs -Uninstall
        $script:log = @()
        $code = Invoke-Logged $PowerShellExe ($uninstallArgs + ' -ListPrograms')
        Check 'uninstall passes -Uninstall' ($code -eq 0 -and $uninstallArgs.EndsWith('" -Uninstall')) $uninstallArgs

        # PathOfExile.tmp is added for every PathOfExile program, once
        $l = Add-GameTmp @('C:\Games\PathOfExile.exe')
        Check 'PathOfExile.tmp is added to a PathOfExile program' (($l -join '|') -eq 'C:\Games\PathOfExile.exe|PathOfExile.tmp') ($l -join '|')
        $l = Add-GameTmp @('C:\Games\PathOfExile.exe', 'D:\x\PathOfExile.tmp')
        Check 'PathOfExile.tmp is not added twice' ($l.Count -eq 2) ($l -join '|')
        $l = Add-GameTmp @('C:\Other\game.exe')
        Check 'PathOfExile.tmp is not added to other programs' ($l.Count -eq 1) ($l -join '|')
        $l = Add-GameTmp @('C:\Games\PathOfExile.exe', 'D:\Games, Steam\PathOfExile.tmp')
        Check 'a found PathOfExile.tmp path becomes the name' (($l -join '|') -eq 'C:\Games\PathOfExile.exe|PathOfExile.tmp') ($l -join '|')

        # Programs with kxgdildr.dll registered are found (test keys in HKCU)
        $root = 'HKCU:\Software\kxgdi-setuptest-' + [Guid]::NewGuid().ToString('N')
        try {
            New-Item $root -Force | Out-Null
            New-Item "$root\foo.exe" -Force | Out-Null
            New-ItemProperty "$root\foo.exe" -Name VerifierDlls -Value 'KexDll.dll kxgdildr.dll' | Out-Null
            New-Item "$root\bar.tmp\filter1" -Force | Out-Null
            New-ItemProperty "$root\bar.tmp\filter1" -Name VerifierDlls -Value 'kxgdildr.dll' | Out-Null
            New-Item "$root\other.exe" -Force | Out-Null
            New-ItemProperty "$root\other.exe" -Name VerifierDlls -Value 'notkxgdildr.dll' | Out-Null
            New-Item "$root\kxgdi-ldrtest.exe" -Force | Out-Null
            New-ItemProperty "$root\kxgdi-ldrtest.exe" -Name VerifierDlls -Value 'kxgdildr.dll' | Out-Null
            $reg = @(Get-RegisteredPrograms $root | ForEach-Object { $_ }) | Sort-Object
            Check 'programs registered by name are found (not per-path VxKex ones, not the test program)' (($reg -join '|') -eq 'foo.exe') ($reg -join '|')
        } finally {
            Remove-Item $root -Recurse -Force -ErrorAction SilentlyContinue
        }

        # The loader log can be read while the loader still writes to it
        # (it opens the file for writing and shares it for reading only)
        $logPath = Join-Path $tmp 'kxgdildr-test.log'
        $writer = New-Object IO.FileStream -ArgumentList $logPath, ([IO.FileMode]::Create), ([IO.FileAccess]::Write), ([IO.FileShare]::Read)
        try {
            $bytes = [Text.Encoding]::Unicode.GetPreamble() + [Text.Encoding]::Unicode.GetBytes("line 1`r`nline 2`r`n")
            $writer.Write($bytes, 0, $bytes.Length)
            $writer.Flush()
            $lines = @()
            try { $lines = Read-SharedLines $logPath } catch { Write-Host "       $_" }
            Check 'loader log is readable while it is open for writing' ($lines[0] -eq 'line 1' -and $lines[1] -eq 'line 2') ($lines -join '|')
        } finally {
            $writer.Close()
        }

        # The window can be built (catches errors in the layout code), and
        # adding a program by hand also adds PathOfExile.tmp
        $ok = $true
        try {
            $form = New-SetupForm
            $form.CreateControl()
            $form.PerformLayout()
            $n = Add-ListItems @('C:\Games\Path of Exile\PathOfExile.exe')
            $n += Add-ListItems @('C:\Games\Path of Exile\PathOfExile.exe')
            $listed = Get-ListedPrograms
            Check 'adding a program by hand adds PathOfExile.tmp, without duplicates' ($n -eq 2 -and ($listed -join '|') -eq 'C:\Games\Path of Exile\PathOfExile.exe|PathOfExile.tmp') ($listed -join '|')

            # A removed PathOfExile.tmp does not come back when something else is added
            $script:ui.List.Items.Remove('PathOfExile.tmp')
            [void] (Add-ListItems @('C:\Other\tool.exe'))
            $listed = Get-ListedPrograms
            Check 'a removed PathOfExile.tmp stays removed' (($listed -join '|') -eq 'C:\Games\Path of Exile\PathOfExile.exe|C:\Other\tool.exe') ($listed -join '|')
            $script:ui.Output = $null
            $form.Dispose()
        } catch { $ok = $false; Write-Host "       $_" }
        Check 'window can be built' $ok
    } finally {
        Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
    }

    Write-Host ''
    if ($failed -eq 0) { Write-Host 'RESULT: all setup checks passed' } else { Write-Host "RESULT: $failed check(s) failed" -ForegroundColor Red }
    exit $failed
}

#
# Snapshot (development): the window with sample content as PNG
#

if ($Snapshot) {
    [Windows.Forms.Application]::EnableVisualStyles()
    $form = New-SetupForm
    $script:ui.Status.Text = 'Windows 7 64-bit Service Pack 1 - OK.'
    $script:ui.Status.ForeColor = [Drawing.Color]::FromArgb(0, 120, 0)
    [void] $script:ui.List.Items.Add('C:\Program Files (x86)\Steam\steamapps\common\Path of Exile\PathOfExileSteam.exe')
    [void] $script:ui.List.Items.Add('PathOfExile.tmp')
    foreach ($line in @(
        '=== Install ===',
        'Programs:',
        '  C:\Program Files (x86)\Steam\steamapps\common\Path of Exile\PathOfExileSteam.exe',
        '  PathOfExile.tmp',
        '(Programs given by file name only are set up in every folder.)',
        'Method: Loader',
        'Copied: C:\Windows\System32\kxgdildr.dll',
        'Copied: C:\Windows\System32\kxgdi.dll',
        '',
        '>>> Installed. Start the game now.')) { Write-Out $line }

    $form.ShowInTaskbar = $false
    $form.Opacity = 0
    $form.Show()
    [Windows.Forms.Application]::DoEvents()
    $bmp = New-Object Drawing.Bitmap -ArgumentList $form.Width, $form.Height
    $form.DrawToBitmap($bmp, (New-Object Drawing.Rectangle -ArgumentList 0, 0, $form.Width, $form.Height))
    $bmp.Save([IO.Path]::GetFullPath($Snapshot), [Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    $form.Close()
    exit 0
}

#
# Main
#

try {
    [Windows.Forms.Application]::EnableVisualStyles()
    $form = New-SetupForm
    $form.Add_Shown({
        Write-Out 'Welcome! Do the three steps from top to bottom:'
        Write-Out '  1. Run tests   2. Check the programs   3. Install'
        Write-Out 'Then start Path of Exile as usual.'
        Invoke-Action { Find-AndAdd }
    })
    [void] $form.ShowDialog()
} catch {
    Show-Message ("kxgdi Setup stopped with an error:`r`n`r`n" + $_) 'Error'
    exit 1
}

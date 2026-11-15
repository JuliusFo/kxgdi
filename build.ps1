<#
.SYNOPSIS
    Builds kxgdi.dll, kxgdildr.dll and the tests (x64) without the Windows SDK.

.DESCRIPTION
    Only needs the MSVC compiler tools (cl, link, lib) from Visual Studio /
    Build Tools. Import libraries for kernel32, gdi32 and ntdll are generated
    from imports\*.def, and the code uses no SDK headers and no C runtime.

    Output in out\x64:
      kxgdi.dll                    gdi32 extension
      kxgdildr.dll                 standalone loader (verifier provider)
      kxgdi-test.exe               self-test of kxgdi.dll
      kxgdildr-unit.exe            unit test of the import rewriting
      kxgdi-ldrtest.exe (+ DLLs)   end-to-end loader test (needs Test-Loader.ps1)

    -Test runs kxgdi-test.exe, kxgdildr-unit.exe and the installer argument
    test; when running as administrator it also runs the end-to-end loader test.
#>
param(
    [switch] $RegenerateDef,
    [switch] $Test,
    # Build server mode: the test tolerates machines without a display adapter.
    [switch] $CI
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -property installationPath
$bin = Get-ChildItem "$vs\VC\Tools\MSVC\*\bin\Hostx64\x64" -Directory |
    Sort-Object FullName -Descending | Select-Object -First 1 -ExpandProperty FullName
if (-not $bin) { throw "MSVC x64 tools not found" }

$cl   = Join-Path $bin 'cl.exe'
$link = Join-Path $bin 'link.exe'
$lib  = Join-Path $bin 'lib.exe'

if ($RegenerateDef -or -not (Test-Path "$root\src\kxgdi.def")) {
    & "$root\tools\gen-def.ps1" -Dumpbin (Join-Path $bin 'dumpbin.exe')
}

$out = Join-Path $root 'out\x64'
$obj = Join-Path $out 'obj'
New-Item -ItemType Directory -Force $obj | Out-Null

function Invoke-Tool($exe, [string[]] $toolArgs) {
    & $exe @toolArgs
    if ($LASTEXITCODE -ne 0) { throw "$(Split-Path $exe -Leaf) failed with exit code $LASTEXITCODE" }
}

function Compile([string] $name, [string] $source) {
    Invoke-Tool $cl ($cflags + @("/Fo$obj\$name.obj", "/Fd$obj\$name.pdb", $source))
}

# The PDB is named after the output file, so one object file can be linked
# into several outputs without them overwriting each other's symbols.
function Link([string] $name, [string] $output, [string[]] $extra) {
    $pdb = [IO.Path]::ChangeExtension($output, '.pdb')
    Invoke-Tool $link ($lflags + $extra + @("/OUT:$out\$output", "/PDB:$out\$pdb", "$obj\$name.obj"))
}

# Import libraries from .def files
foreach ($d in 'kernel32', 'gdi32', 'ntdll') {
    Invoke-Tool $lib @('/nologo', "/def:$root\imports\$d.def", '/machine:x64', "/out:$obj\$d.lib")
}

# Common flags: no CRT, no stack cookies, no SDK headers. Subsystem 6.01 = loadable on Windows 7.
$cflags = @('/nologo', '/c', '/O2', '/W4', '/WX', '/GS-', '/Gs1000000', '/Zl', '/Zi')
$lflags = @('/nologo', '/NODEFAULTLIB', '/MACHINE:X64', '/DEBUG', '/OPT:REF', '/OPT:ICF',
            '/DYNAMICBASE', '/NXCOMPAT', "/LIBPATH:$obj", "/LIBPATH:$out")
$dll = @('/DLL', '/ENTRY:DllMain', '/SUBSYSTEM:WINDOWS,6.01')
$exe = @('/ENTRY:TestMain', '/SUBSYSTEM:CONSOLE,6.01')

# kxgdi.dll
Compile 'kxgdi' "$root\src\kxgdi.c"
Link 'kxgdi' 'kxgdi.dll' ($dll + @("/DEF:$root\src\kxgdi.def", 'kernel32.lib', 'gdi32.lib', 'ntdll.lib'))

# kxgdildr.dll (ntdll only: it runs before kernel32 is initialized)
Compile 'kxgdildr' "$root\src\kxgdildr.c"
Link 'kxgdildr' 'kxgdildr.dll' ($dll + @('ntdll.lib'))

# Tests
Compile 'test' "$root\test\test.c"
Link 'test' 'kxgdi-test.exe' ($exe + @('kernel32.lib', 'gdi32.lib', 'ntdll.lib'))

Compile 'ldrtest-dll' "$root\test\ldrtest-dll.c"
Link 'ldrtest-dll' 'kxgdi-ldrtest-static.dll' ($dll + @('gdi32.lib', "/IMPLIB:$obj\kxgdi-ldrtest-static.lib"))
Link 'ldrtest-dll' 'kxgdi-ldrtest-dynamic.dll' ($dll + @('gdi32.lib', "/IMPLIB:$obj\kxgdi-ldrtest-dynamic.lib"))

Compile 'ldrtest' "$root\test\ldrtest.c"
Link 'ldrtest' 'kxgdi-ldrtest.exe' ($exe + @('kernel32.lib', 'gdi32.lib', 'ntdll.lib', 'kxgdi-ldrtest-static.lib'))

Compile 'ldrunit' "$root\test\ldrunit.c"
Link 'ldrunit' 'kxgdildr-unit.exe' ($exe + @('kernel32.lib', 'ntdll.lib'))

Write-Host "Built kxgdi.dll, kxgdildr.dll and tests in $out"

if ($Test) {
    Push-Location $out
    try {
        if ($CI) { & .\kxgdi-test.exe /ci } else { & .\kxgdi-test.exe }
        if ($LASTEXITCODE -ne 0) { throw "kxgdi-test.exe reported $LASTEXITCODE failed check(s)" }

        & .\kxgdildr-unit.exe
        if ($LASTEXITCODE -ne 0) { throw "kxgdildr-unit.exe reported $LASTEXITCODE failed check(s)" }
    } finally {
        Pop-Location
    }

    # Through Windows PowerShell on purpose: how arguments reach
    # "powershell -File" depends on the calling shell (see the test).
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$root\test\Test-InstallerArgs.ps1"
    if ($LASTEXITCODE -ne 0) { throw "Test-InstallerArgs.ps1 reported $LASTEXITCODE failed check(s)" }

    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        & "$root\test\Test-Loader.ps1" -Dir $out
        if ($LASTEXITCODE -ne 0) { throw "loader test failed" }
    } else {
        Write-Host 'Skipping the end-to-end loader test (needs administrator rights: test\Test-Loader.ps1).'
    }
}

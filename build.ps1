<#
.SYNOPSIS
    Builds kxgdi.dll (x64) and kxgdi-test.exe without the Windows SDK.

.DESCRIPTION
    Only needs the MSVC compiler tools (cl, link, lib) from Visual Studio /
    Build Tools. Import libraries for kernel32 and gdi32 are generated from
    imports\*.def, and the code uses no SDK headers and no C runtime.

    Output: out\x64\kxgdi.dll, out\x64\kxgdi-test.exe
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

# Import libraries from .def files
foreach ($d in 'kernel32', 'gdi32') {
    Invoke-Tool $lib @('/nologo', "/def:$root\imports\$d.def", '/machine:x64', "/out:$obj\$d.lib")
}

# Common flags: no CRT, no stack cookies, no SDK headers
$cflags = @('/nologo', '/c', '/O2', '/W4', '/WX', '/GS-', '/Gs1000000', '/Zl', '/Zi')
$lflags = @('/nologo', '/NODEFAULTLIB', '/MACHINE:X64', '/DEBUG', '/OPT:REF', '/OPT:ICF',
            '/DYNAMICBASE', '/NXCOMPAT', "/LIBPATH:$obj", 'kernel32.lib', 'gdi32.lib')

# kxgdi.dll  (subsystem 6.01 = loadable on Windows 7)
Invoke-Tool $cl ($cflags + @("/Fo$obj\kxgdi.obj", "/Fd$obj\kxgdi.pdb", "$root\src\kxgdi.c"))
Invoke-Tool $link ($lflags + @('/DLL', '/ENTRY:DllMain', '/SUBSYSTEM:WINDOWS,6.01',
    "/DEF:$root\src\kxgdi.def", "/OUT:$out\kxgdi.dll", "/PDB:$out\kxgdi.pdb", "$obj\kxgdi.obj"))

# kxgdi-test.exe
Invoke-Tool $cl ($cflags + @("/Fo$obj\test.obj", "/Fd$obj\test.pdb", "$root\test\test.c"))
Invoke-Tool $link ($lflags + @('/ENTRY:TestMain', '/SUBSYSTEM:CONSOLE,6.01',
    "/OUT:$out\kxgdi-test.exe", "/PDB:$out\kxgdi-test.pdb", "$obj\test.obj"))

Write-Host "Built $out\kxgdi.dll and $out\kxgdi-test.exe"

if ($Test) {
    Push-Location $out
    try {
        if ($CI) { & .\kxgdi-test.exe /ci } else { & .\kxgdi-test.exe }
        $code = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    if ($code -ne 0) { throw "kxgdi-test.exe reported $code failed check(s)" }
}

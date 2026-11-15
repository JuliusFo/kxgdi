# kxgdi – missing gdi32 functions for Windows 7 (x64)

[Deutsche Version](README.de.md)

`kxgdi.dll` provides `gdi32.dll` functions that only exist on Windows 8 and later. All other gdi32
functions (994 of them) are forwarded unchanged to the real `gdi32.dll`. It can be used on its own with
the included loader, or together with [VxKex](https://github.com/i486/VxKex) (including VxKex NEXT).

Implemented so far:

| Function | Since | Emulation on Windows 7 |
|---|---|---|
| `D3DKMTOpenAdapterFromLuid` | Windows 8 | Enumerate all display adapters (SetupAPI device interfaces, falling back to GDI display names), open each with `D3DKMTOpenAdapterFromDeviceName` / `D3DKMTOpenAdapterFromGdiDisplayName`, compare the LUID, return the matching handle. |

Motivation: recent versions of Path of Exile no longer start on Windows 7 ("The procedure entry
point D3DKMTOpenAdapterFromLuid could not be located in the dynamic link library GDI32.dll").

> [!WARNING]
> **Experimental.** This project is not affiliated with Grinding Gear Games or with VxKex.
> Windows 7 is not supported by Path of Exile. kxgdi loads code into the game process;
> a ban is unlikely but cannot be ruled out. Use at your own risk.

## Status

- ✅ Built and tested on Windows 11: `kxgdi-test.exe` (both lookup strategies find the adapter, results
  match the native Windows implementation) and the unit test of the loader's import rewriting.
- ✅ The end-to-end loader test (`Test-Loader.ps1`) runs on every build on GitHub Actions (Windows Server).
  It covers the game's own imports there. Rewriting the imports of DLLs and switching off Application
  Verifier only happen on Windows 7 and can only be checked there.
- ✅ All DLLs only import functions that exist on Windows 7 and are marked as Windows 6.01.
  `kxgdildr.dll` only imports `ntdll.dll`.
- ⚠️ **Not yet confirmed on Windows 7.** Please run the two tests below first and report the result.
- x64 only (Path of Exile is 64-bit).

## Before you start

- **Graphics issues remain:** even if Path of Exile starts, the Windows 7 DirectX 11 runtime still
  rejects the new particle compute shaders, and older Vulkan drivers may crash. For effects you will
  most likely need [DXVK](https://github.com/doitsujin/dxvk) 1.10.3 (the last version for Vulkan 1.2).
- There may be more missing functions. Windows only reports the first one.

## Installation (on the Windows 7 PC)

**Requirements:** Windows 7 SP1 x64. VxKex is **not** required.

1. **Extract the release zip**, e.g. to `C:\kxgdi`.

2. **Self-test of kxgdi:** open a command prompt and run:
   ```
   cd /d C:\kxgdi
   kxgdi-test.exe
   ```
   Expected: many `[ OK ]` lines and `RESULT: all checks passed` at the end. The line
   `No native D3DKMTOpenAdapterFromLuid (Windows 7)` is expected.

3. **Loader test:** open PowerShell **as administrator** (64-bit, not "x86") and run:
   ```
   cd C:\kxgdi
   powershell -NoProfile -ExecutionPolicy Bypass -File .\Test-Loader.ps1
   ```
   This starts a small test program that imports the missing function exactly like the game, and removes
   everything again afterwards. Expected: `Loader test passed`.
   If either test shows `[FAIL]` or the test program cannot start, please open an issue with the output
   and **do not continue**.

4. **Find the path of the `.tmp` file.** The error message mentions `PathOfExile.tmp`. While the error
   dialog is open: Task Manager → "Processes" tab → right-click `PathOfExile.tmp` → "Open file location".

5. **Install** (PowerShell as administrator, adjust the paths, list both programs):
   ```
   powershell -NoProfile -ExecutionPolicy Bypass -File .\Install-KxGdi.ps1 -ExePath "C:\Program Files (x86)\Steam\steamapps\common\Path of Exile\PathOfExileSteam.exe","<path to PathOfExile.tmp>"
   ```
   The script copies `kxgdildr.dll` and `kxgdi.dll` to `C:\Windows\System32` and registers the loader
   for these two programs only. If VxKex (or VxKex NEXT) is enabled for a program, the loader is added
   after it. If you enable or disable VxKex for the game later, run the script again. The script only
   installs on Windows 7 (newer Windows versions have the function natively).

6. **Watch the log (optional but helpful):** start Sysinternals **DebugView**. You will see `KxGdiLdr:`
   lines from the loader and `KxGdi:` lines for every call of the function. The loader also writes its
   log to `C:\Windows\Temp\kxgdildr-<program>.log` (e.g. `kxgdildr-PathOfExile.tmp.log`), even if the
   game does not start. Please attach it to bug reports.

7. **Start the game.** Possible outcomes:
   - It starts → next step is DXVK for effects.
   - A new "entry point … not found" message → please open an issue with a screenshot. That is the next
     missing function.
   - A crash → please attach the Event Viewer entry (Windows Logs → Application, "Application Error")
     and the DebugView output.

**Uninstall:**
```
powershell -NoProfile -ExecutionPolicy Bypass -File .\Install-KxGdi.ps1 -ExePath "<same paths as above>" -Uninstall
```
This removes the loader registration and, if no other program uses it, the two DLLs from System32.
Close the game first.

### Alternative: via VxKex (i486/VxKex 1.2.0 or later)

Instead of the loader, kxgdi can be plugged into [i486/VxKex](https://github.com/i486/VxKex/releases)
1.2.0 or later via its `KEX_DllRewriteEntries` setting (VxKex NEXT does not support this setting):
```
powershell -NoProfile -ExecutionPolicy Bypass -File .\Install-KxGdi.ps1 -Method VxKex -ExePath "<paths>"
```
This copies `kxgdi.dll` to `<VxKex>\Kex64` and enables VxKex for the programs. Uninstall with the same
command plus `-Uninstall`.

## How it works

- **Loader:** `kxgdildr.dll` is registered as an Application Verifier provider for the game only
  (IFEO values `GlobalFlag`, `VerifierFlags`, `VerifierDlls`; the same mechanism VxKex uses). Windows
  loads it before the game's imports are resolved. It renames the `gdi32.dll` entries in the import
  tables of the game and of every DLL outside the Windows folder (except `Windows\Temp`) to `kxgdi.dll`
  (same length, changed in memory only), and removes the bound import table of images it changed.
  Files on disk are not modified. Since kernel32 is not usable at that point, the loader only uses ntdll;
  it writes its log to `Windows\Temp` and keeps it in memory for kxgdi.dll, which prints it to DebugView.
  On Windows 7 the loader then switches off most of Application Verifier (heap and handle tracking),
  like VxKex does.
- **Limitation:** a game DLL that loads another DLL with a restricted search path (for example only its
  own folder, without System32) cannot find `kxgdi.dll` once its gdi32 import is renamed. No such case
  is known for Path of Exile.
- **With VxKex:** the installer puts VxKex's `KexDll.dll` first in `VerifierDlls` and kxgdildr last;
  kxgdildr then leaves Application Verifier to VxKex.
- **kxgdi.dll:** implements the missing functions and forwards everything else to `gdi32.dll`. The
  forwarder list was generated from the Windows 11 `gdi32.dll` (995 names). Forwarders to names that
  do not exist on Windows 7 are harmless: they only fail if a program imports exactly that function,
  which would fail without kxgdi as well.

## Building

Requirements: Visual Studio or Build Tools with the MSVC x64 tools. The Windows SDK is **not** required:
the code uses no SDK headers and no C runtime, and the import libraries are generated from
`imports\*.def`.

```
pwsh .\build.ps1 -Test              # builds everything in out\x64 and runs the tests
pwsh .\build.ps1 -RegenerateDef     # regenerate the forwarder list from the local gdi32.dll
pwsh .\tools\gen-def.ps1 -Gdi32 C:\Windows\System32\gdi32.dll, D:\win7\gdi32.dll   # merge Windows 7 exports
pwsh .\tools\package.ps1            # dist\kxgdi-win7-x64.zip
```

`build.ps1 -Test` runs `kxgdi-test.exe` and `kxgdildr-unit.exe`; as administrator it also runs the
end-to-end `test\Test-Loader.ps1`. On build servers without a display adapter the kxgdi test runs with
`/ci` and skips the adapter checks.

Release builds are made by GitHub Actions (`.github/workflows/build.yml`): run the workflow manually
with a version such as `v0.2.0`. It creates a draft pre-release with the zip, checksums and the matching
section of [CHANGELOG.md](CHANGELOG.md); publishing the draft creates the tag.

Adding functions: implement them as `Ext_<Name>` in `src\kxgdi.c`, add them to `$Implemented` in
`tools\gen-def.ps1`, then run `build.ps1 -RegenerateDef -Test`.

| File | Contents |
|---|---|
| `src\kxgdi.c` | Implementation of the missing functions |
| `src\kxgdi.def` | Exports (generated) |
| `src\kxgdildr.c` | Standalone loader (Application Verifier provider, ntdll only) |
| `src\kxtypes.h` | Minimal Windows/NT types for the loader and its tests |
| `imports\*.def` | Import libraries without the SDK (`gdi32.def` generated) |
| `test\test.c` | Self-test of kxgdi.dll, runs on Windows 7 and later |
| `test\ldrunit.c` | Unit test of the import rewriting |
| `test\ldrtest.c`, `test\ldrtest-dll.c`, `test\Test-Loader.ps1` | End-to-end loader test (administrator) |
| `install\Install-KxGdi.ps1` | Installer (PowerShell 2.0) |
| `tools\gen-def.ps1`, `tools\package.ps1` | Generator and packaging |
| `.github\workflows\build.yml` | CI build and releases |

## License

[MIT](LICENSE). Not affiliated with Microsoft, Grinding Gear Games or the VxKex authors.

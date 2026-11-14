# kxgdi – gdi32 extension for VxKex (Windows 7, x64)

[Deutsche Version](README.de.md)

`kxgdi.dll` adds `gdi32.dll` functions that only exist on Windows 8 and later to
[VxKex](https://github.com/i486/VxKex). All other gdi32 functions (994 of them) are forwarded
unchanged to the real `gdi32.dll`.

Implemented so far:

| Function | Since | Emulation on Windows 7 |
|---|---|---|
| `D3DKMTOpenAdapterFromLuid` | Windows 8 | Enumerate all display adapters (SetupAPI device interfaces, falling back to GDI display names), open each with `D3DKMTOpenAdapterFromDeviceName` / `D3DKMTOpenAdapterFromGdiDisplayName`, compare the LUID, return the matching handle. |

Motivation: recent versions of Path of Exile no longer start on Windows 7 ("The procedure entry
point D3DKMTOpenAdapterFromLuid could not be located in the dynamic link library GDI32.dll").

> [!WARNING]
> **Experimental.** This project is not affiliated with Grinding Gear Games or with VxKex.
> Windows 7 is not supported by Path of Exile. kxgdi and VxKex load code into the game process;
> a ban is unlikely but cannot be ruled out. Use at your own risk.

## Status

- ✅ Built and tested on Windows 11 (`kxgdi-test.exe`): both lookup strategies find the adapter, return
  values match the native Windows implementation (success, or `0xC000000D` for an unknown LUID), and
  forwarded functions resolve to `gdi32.dll`.
- ✅ The DLL only imports functions that exist on Windows 7 and is marked as Windows 6.01.
- ⚠️ **Not yet confirmed on Windows 7.** Please run `kxgdi-test.exe` first and report the result.
- x64 only (Path of Exile is 64-bit).

## Before you start

- **Graphics issues remain:** even if Path of Exile starts, the Windows 7 DirectX 11 runtime still
  rejects the new particle compute shaders, and older Vulkan drivers may crash. For effects you will
  most likely need [DXVK](https://github.com/doitsujin/dxvk) 1.10.3 (the last version for Vulkan 1.2).
- There may be more missing functions. Windows only reports the first one.

## Installation (on the Windows 7 PC)

**Requirements:** Windows 7 SP1 x64 and **VxKex 1.2.0 or later** from
[i486/VxKex](https://github.com/i486/VxKex/releases). Older versions and *VxKex NEXT* do not support the
`KEX_DllRewriteEntries` setting that kxgdi relies on.

1. **Extract the release zip**, e.g. to `C:\kxgdi`.

2. **Self-test:** open a command prompt and run:
   ```
   cd /d C:\kxgdi
   kxgdi-test.exe
   ```
   Expected: many `[ OK ]` lines and `RESULT: all checks passed` at the end. The line
   `No native D3DKMTOpenAdapterFromLuid (Windows 7)` is expected.
   If you see `[FAIL]`, please open an issue with the output and **do not continue**.

3. **Find the path of the `.tmp` file.** The error message mentions `PathOfExile.tmp`. While the error
   dialog is open: Task Manager → "Processes" tab → right-click `PathOfExile.tmp` → "Open file location".

4. **Install:** open PowerShell **as administrator** (64-bit, not "x86") and run (adjust the paths,
   list both programs):
   ```
   cd C:\kxgdi
   powershell -NoProfile -ExecutionPolicy Bypass -File .\Install-KxGdi.ps1 -ExePath "C:\Program Files (x86)\Steam\steamapps\common\Path of Exile\PathOfExileSteam.exe","<path to PathOfExile.tmp>"
   ```
   The script
   - copies `kxgdi.dll` to `<VxKex>\Kex64\`,
   - enables VxKex for the given programs,
   - sets `KEX_DllRewriteEntries = gdi32:kxgdi|api-ms-win-dx-d3dkmt:kxgdi` in their VxKex settings.
     Existing entries are kept.

5. **Watch the log (optional but helpful):** start Sysinternals **DebugView**. kxgdi logs every call as
   `KxGdi: D3DKMTOpenAdapterFromLuid(...) -> ...`.

6. **Start the game.** Possible outcomes:
   - It starts → next step is DXVK for effects.
   - A new "entry point … not found" message → please open an issue with a screenshot. That is the next
     missing function.
   - A crash → please attach the Event Viewer entry (Windows Logs → Application, "Application Error")
     and the DebugView output.

**Uninstall:**
```
powershell -NoProfile -ExecutionPolicy Bypass -File .\Install-KxGdi.ps1 -ExePath "<same paths as above>" -Uninstall
```
This removes the rewrite entries and `kxgdi.dll`. VxKex itself stays enabled and can be disabled in the
file's Properties dialog ("VxKex" tab).

## How it works

- VxKex rewrites DLL names in the import tables of a program's DLLs while they are loaded. Since VxKex
  1.2.0 the per-program IFEO value `KEX_DllRewriteEntries` extends that table without rebuilding VxKex.
  The new name must not be longer than the old one: `kxgdi` and `gdi32` both have 5 characters.
- `kxgdi.dll` must live in `<VxKex>\Kex64`. VxKex does not rewrite DLLs in that folder, so kxgdi's own
  gdi32 imports reach the real `gdi32.dll`. Anywhere else, VxKex would redirect kxgdi's gdi32 imports
  to kxgdi itself.
- The forwarder list was generated from the Windows 11 `gdi32.dll` (995 names). Forwarders to names that
  do not exist on Windows 7 are harmless: they only fail if a program imports exactly that function,
  which would fail without kxgdi as well.

## Building

Requirements: Visual Studio or Build Tools with the MSVC x64 tools. The Windows SDK is **not** required:
the code uses no SDK headers and no C runtime, and the import libraries are generated from
`imports\*.def`.

```
pwsh .\build.ps1 -Test              # builds out\x64\kxgdi.dll + kxgdi-test.exe and runs the test
pwsh .\build.ps1 -RegenerateDef     # regenerate the forwarder list from the local gdi32.dll
pwsh .\tools\gen-def.ps1 -Gdi32 C:\Windows\System32\gdi32.dll, D:\win7\gdi32.dll   # merge Windows 7 exports
pwsh .\tools\package.ps1            # dist\kxgdi-win7-x64.zip
```

Release builds are made by GitHub Actions from the tagged source (`.github/workflows/build.yml`).
On build servers without a display adapter the test runs with `/ci` and skips the adapter checks.

Adding functions: implement them as `Ext_<Name>` in `src\kxgdi.c`, add them to `$Implemented` in
`tools\gen-def.ps1`, then run `build.ps1 -RegenerateDef -Test`.

| File | Contents |
|---|---|
| `src\kxgdi.c` | Implementation |
| `src\kxgdi.def` | Exports (generated) |
| `imports\*.def` | Import libraries without the SDK (`gdi32.def` generated) |
| `test\test.c` | Self-test, runs on Windows 7 and later |
| `install\Install-KxGdi.ps1` | Installer for the Windows 7 PC (PowerShell 2.0) |
| `tools\gen-def.ps1`, `tools\package.ps1` | Generator and packaging |
| `.github\workflows\build.yml` | CI build and releases |

## License

[MIT](LICENSE). Not affiliated with Microsoft, Grinding Gear Games or the VxKex authors.

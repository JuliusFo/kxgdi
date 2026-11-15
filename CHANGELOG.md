# Changelog

## v0.2.0

- **New: standalone loader `kxgdildr.dll`.** kxgdi no longer needs VxKex. The loader is registered as an
  Application Verifier provider for the game only, and renames the game's `gdi32.dll` imports to
  `kxgdi.dll` before they are resolved. It also works alongside VxKex NEXT and i486/VxKex.
- `Install-KxGdi.ps1` uses the loader by default (`-Method Loader`). The previous method via
  `KEX_DllRewriteEntries` (i486/VxKex 1.2.0+) is still available with `-Method VxKex`.
- New end-to-end test `Test-Loader.ps1` (needs administrator rights): runs a test program that imports
  `D3DKMTOpenAdapterFromLuid` from `gdi32.dll` like the game, then removes everything again.
- Loader log: written to `C:\Windows\Temp\kxgdildr-<program>.log` (also when the program fails to
  start) and printed to DebugView by kxgdi.dll (`KxGdiLdr:` lines).
- The installer only installs on Windows 7, accepts relative paths and differently spelled paths, and
  keeps VxKex's `KexDll.dll` first in `VerifierDlls`.
- `D3DKMTOpenAdapterFromLuid` caches which adapter belongs to a LUID: only the first call enumerates all
  adapters, later calls open just that one. It now writes debug output only for the first lookup per
  adapter and for failures (at most 16), and returns `STATUS_INVALID_PARAMETER` for an invalid pointer
  instead of crashing, like Windows 8+.
- `kxgdi-test.exe` compares every status code exactly with the native Windows 8+ function, tests the
  cache, invalid pointers and handle leaks. The loader unit test checks that page protections are
  restored.
- The loader log is printed to DebugView line by line, so long logs are no longer cut off.
- `tools\gen-def.ps1` stops instead of writing incomplete `.def` files when `dumpbin` fails or the
  input is not a `gdi32.dll`.

**Not yet confirmed on Windows 7.** Please run `Test-Loader.ps1` and `kxgdi-test.exe` and report the
result in an issue.

## v0.1.0

First experimental release.

- Adds `D3DKMTOpenAdapterFromLuid` (Windows 8+) for Windows 7 x64 via VxKex. Fixes the startup error
  "The procedure entry point D3DKMTOpenAdapterFromLuid could not be located in the dynamic link library
  GDI32.dll", e.g. in recent versions of Path of Exile.
- All other gdi32 functions are forwarded unchanged to the real `gdi32.dll`.
- `kxgdi-test.exe`: self-test, run it first on the target PC.
- `Install-KxGdi.ps1`: installer for VxKex 1.2.0 or later (from i486/VxKex; VxKex NEXT is not supported).

**Not yet confirmed on Windows 7.** Please run `kxgdi-test.exe` and report the result in an issue,
including `[FAIL]` lines, crashes or the next "entry point not found" message.

Graphics effects in Path of Exile on Windows 7 additionally need DXVK 1.10.3; kxgdi only fixes the
startup error.

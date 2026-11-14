# Changelog

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

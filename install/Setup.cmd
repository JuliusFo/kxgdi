@echo off
rem Opens the kxgdi setup window. Double-click this file.
rem The window asks for administrator rights itself.

if exist "%~dp0Setup-KxGdi.ps1" goto run
echo Setup-KxGdi.ps1 was not found next to Setup.cmd.
echo Please extract the whole zip file first (right-click the zip, "Extract All..."),
echo then start Setup.cmd in the extracted folder.
echo.
pause
exit /b 1

:run
set "KXPS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "KXPS=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"

rem The script restarts itself elevated with a hidden console; this window
rem closes then. It stays open only if PowerShell could not run the script.
"%KXPS%" -NoProfile -ExecutionPolicy Bypass -STA -File "%~dp0Setup-KxGdi.ps1"
if errorlevel 1 pause

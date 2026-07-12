@echo off
REM Double-click entry for the safe, sandboxed demo attack.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-Attack.ps1"
echo.
pause

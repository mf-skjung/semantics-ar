@echo off
REM semantics-ar Demo Kit - one-click setup for a throwaway demo VM.
REM TEST MODE. Not production-signed. Run only on a disposable VM.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-SarDemo.ps1"
echo.
pause

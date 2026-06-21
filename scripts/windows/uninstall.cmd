@echo off
setlocal

net session >nul 2>&1
if errorlevel 1 (
    echo Administrator privileges required.
    exit /b 1
)

fltmc unload SemanticsAR
sc stop SemanticsAR
sc delete SemanticsAR

echo Uninstall complete.
endlocal
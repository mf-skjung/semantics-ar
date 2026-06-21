@echo off
setlocal

net session >nul 2>&1
if errorlevel 1 (
    echo Administrator privileges required.
    exit /b 1
)

set ROOT=%~dp0..\..
set DRIVER=%ROOT%\driver\x64\Release\semantics_ar.sys
set INF=%ROOT%\driver\semantics_ar.inf

if not exist "%DRIVER%" (
    echo Driver binary not found: %DRIVER%
    exit /b 1
)

RUNDLL32.EXE SETUPAPI.DLL,InstallHinfSection DefaultInstall 132 "%INF%"
if errorlevel 1 exit /b 1

sc start SemanticsAR
fltmc load SemanticsAR

echo Install complete.
endlocal
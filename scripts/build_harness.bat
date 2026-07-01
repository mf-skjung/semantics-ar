@echo off
setlocal enabledelayedexpansion

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VSPATH=%%i
if not defined VSPATH ( echo vswhere could not locate Visual Studio & exit /b 1 )
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || ( echo vcvars64 failed & exit /b 1 )

pushd "%~dp0.."
set REPO=%CD%
popd

set OUT=%1
if "%OUT%"=="" set OUT=%REPO%\build_harness
if not exist "%OUT%" mkdir "%OUT%"

set SRC=%REPO%\tests\harness

echo === compiling user-mode harnesses ===
for %%F in ("%SRC%\*.c") do (
  cl /nologo /O2 /MT /W3 "%%F" /Fe"%OUT%\%%~nF.exe" /Fo"%OUT%\%%~nF.obj" /link bcrypt.lib || ( echo COMPILE FAIL %%~nF & exit /b 2 )
)
del "%OUT%\*.obj" >nul 2>&1

echo === HARNESSES OK: %OUT% ===
endlocal

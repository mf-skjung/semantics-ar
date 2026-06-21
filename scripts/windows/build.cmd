@echo off
setlocal

set ROOT=%~dp0..\..
set BUILD=%ROOT%\build

if not exist "%BUILD%" mkdir "%BUILD%"

cmake -S "%ROOT%" -B "%BUILD%" -A x64 -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1

cmake --build "%BUILD%" --config Release
if errorlevel 1 exit /b 1

echo Service build complete: %BUILD%\service\Release\SemanticsARService.exe
echo Build the driver separately with the WDK: msbuild driver\semantics_ar.vcxproj /p:Configuration=Release /p:Platform=x64
endlocal
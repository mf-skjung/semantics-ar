@echo off
setlocal enabledelayedexpansion

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath`) do set VSPATH=%%i
if not defined VSPATH ( echo vswhere could not locate Visual Studio & exit /b 1 )
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || ( echo vcvars64 failed & exit /b 1 )

pushd "%~dp0.."
set REPO=%CD%
popd

set OUT=%1
if "%OUT%"=="" set OUT=%REPO%\build_driver
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%"

set SDKI=%WindowsSdkDir%Include\%WindowsSDKVersion%
set SDKL=%WindowsSdkDir%Lib\%WindowsSDKVersion%
set INCLUDE=%SDKI%km;%SDKI%km\crt;%VCToolsInstallDir%include;%SDKI%ucrt;%SDKI%shared
set LIB=%VCToolsInstallDir%lib\x64;%SDKL%km\x64;%SDKL%ucrt\x64

set DEFS=/D_AMD64_ /DAMD64 /D_WIN64 /DNTDDI_VERSION=0x0A000010 /D_WIN32_WINNT=0x0A00 /DPOOL_NX_OPTIN=1
set INCS=/I"%REPO%\driver" /I"%REPO%\control\include" /I"%REPO%\common\include" /I"%REPO%\capture\include" /I"%REPO%\engine\include" /I"%REPO%\engine\src" /I"%REPO%\engine\ciphers"
set CF=/nologo /kernel /GS- /Z7 /O2 /c /W3 /wd4005 %DEFS% %INCS%

echo === compiling driver ===
for %%F in (driver operations seam keystore_persist store_io preserve phantom commport recovery state processnotify feature bypassio eventlog) do (
  cl %CF% /Fo"%OUT%\drv_%%F.obj" "%REPO%\driver\%%F.c" || ( echo COMPILE FAIL %%F & exit /b 2 )
)
cl %CF% /Fo"%OUT%\drv_capture.obj"  "%REPO%\driver\capture.c"      || ( echo COMPILE FAIL driver\capture & exit /b 2 )
cl %CF% /Fo"%OUT%\core_capture.obj" "%REPO%\capture\src\capture.c" || ( echo COMPILE FAIL capture core & exit /b 2 )

echo === compiling control ===
for %%F in (msg whitelist mode handshake) do (
  cl %CF% /Fo"%OUT%\ctl_%%F.obj" "%REPO%\control\src\%%F.c" || ( echo COMPILE FAIL ctl %%F & exit /b 2 )
)

echo === compiling engine ===
for %%F in (battery modes gate keystore keystore_mgr preserve recover sha256) do (
  cl %CF% /Fo"%OUT%\eng_%%F.obj" "%REPO%\engine\src\%%F.c" || ( echo COMPILE FAIL eng %%F & exit /b 2 )
)
for %%F in (aes des sm4 camellia aria seed stream) do (
  cl %CF% /Fo"%OUT%\cph_%%F.obj" "%REPO%\engine\ciphers\%%F.c" || ( echo COMPILE FAIL cipher %%F & exit /b 2 )
)

echo === linking ===
link /nologo /DRIVER /INTEGRITYCHECK /SUBSYSTEM:NATIVE,10.00 /ENTRY:DriverEntry /NODEFAULTLIB ^
  /DEBUG /PDB:"%OUT%\semantics_ar.pdb" /OUT:"%OUT%\semantics_ar.sys" ^
  "%OUT%\*.obj" fltMgr.lib ntoskrnl.lib hal.lib ksecdd.lib cng.lib || ( echo LINK FAIL & exit /b 3 )

echo === BUILD OK: %OUT%\semantics_ar.sys ===

echo === compiling elam ===
cl %CF% /Fo"%OUT%\elam.obj" "%REPO%\elam\elam.c" || ( echo COMPILE FAIL elam & exit /b 2 )
echo === linking elam ===
link /nologo /DRIVER /INTEGRITYCHECK /SUBSYSTEM:NATIVE,10.00 /ENTRY:DriverEntry /NODEFAULTLIB ^
  /DEBUG /PDB:"%OUT%\semantics_ar_elam.pdb" /OUT:"%OUT%\semantics_ar_elam.sys" ^
  "%OUT%\elam.obj" ntoskrnl.lib hal.lib || ( echo LINK FAIL elam & exit /b 3 )

echo === BUILD OK: %OUT%\semantics_ar_elam.sys ===
endlocal

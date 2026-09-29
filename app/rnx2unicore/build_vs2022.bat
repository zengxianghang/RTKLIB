@echo off
setlocal

echo ==========================================
echo Build rnx2unicore for RTKLIB
echo ==========================================

where cl >nul 2>nul
if %errorlevel%==0 goto BUILD

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo ERROR: Cannot find vswhere.exe
    if not defined CI pause
    exit /b 1
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%i"

if not defined VS_PATH (
    echo ERROR: Visual Studio with C++ tools was not found
    if not defined CI pause
    exit /b 1
)

call "%VS_PATH%\Common7\Tools\VsDevCmd.bat" -arch=x64
if errorlevel 1 (
    echo ERROR: Failed to initialize Visual Studio environment
    if not defined CI pause
    exit /b 1
)

:BUILD

echo.
echo Compiling rnx2unicore.exe ...

cl /nologo /O2 ^
/DWIN32 ^
/D_USE_MATH_DEFINES ^
rnx2unicore.c ^
..\..\src\rtkcmn.c ^
..\..\src\rinex.c ^
..\..\src\ephemeris.c ^
..\..\src\pntpos.c ^
..\..\src\rtkpos.c ^
..\..\src\ppp.c ^
..\..\src\ppp_ar.c ^
..\..\src\preceph.c ^
..\..\src\ionex.c ^
..\..\src\sbas.c ^
..\..\src\qzslex.c ^
..\..\src\rtcm.c ^
..\..\src\rtcm2.c ^
..\..\src\rtcm3.c ^
..\..\src\rtcm3e.c ^
..\..\src\lambda.c ^
..\..\src\unicore_gpscnav.c ^
/I..\..\src ^
/Fe:rnx2unicore.exe ^
/link winmm.lib

if errorlevel 1 (
    echo.
    echo ==========================================
    echo BUILD FAILED
    echo ==========================================
    if not defined CI pause
    exit /b 1
)

echo.
echo ==========================================
echo BUILD SUCCESS
echo ==========================================
echo Output: %CD%\rnx2unicore.exe
if not defined CI pause

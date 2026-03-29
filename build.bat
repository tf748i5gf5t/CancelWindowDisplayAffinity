@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo === CancelWindowDisplayAffinity Build Script ===
echo.

REM Find Visual Studio via vswhere
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERR] vswhere.exe not found. Is Visual Studio installed?
    exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR ( echo [ERR] Visual Studio not found. & exit /b 1 )
set "VCVARS=%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS%" ( echo [ERR] vcvarsall.bat not found. & exit /b 1 )
echo Found VS: %VSDIR%
echo.

REM Step 1: Compile 32-bit DLL
echo [1/4] Building Affinity32.dll (x86)...
cmd /c ""%VCVARS%" x86 >nul 2>&1 && cl /nologo /DBUILD_DLL /LD /Fe:Affinity32.dll CancelWindowDisplayAffinity.cpp user32.lib >nul 2>&1"
if not exist Affinity32.dll ( echo FAILED & exit /b 1 )
echo       OK

REM Step 2: Compile 64-bit DLL
echo [2/4] Building Affinity64.dll (x64)...
cmd /c ""%VCVARS%" x64 >nul 2>&1 && cl /nologo /DBUILD_DLL /LD /Fe:Affinity64.dll CancelWindowDisplayAffinity.cpp user32.lib >nul 2>&1"
if not exist Affinity64.dll ( echo FAILED & exit /b 1 )
echo       OK

REM Step 3: Compile resource file
echo [3/4] Compiling payload.rc...
cmd /c ""%VCVARS%" x64 >nul 2>&1 && rc /nologo payload.rc >nul 2>&1"
if not exist payload.res ( echo FAILED & exit /b 1 )
echo       OK

REM Step 4: Link final EXE
echo [4/4] Building CancelWindowDisplayAffinity.exe (x64)...
cmd /c ""%VCVARS%" x64 >nul 2>&1 && cl /nologo /EHsc /Fe:CancelWindowDisplayAffinity.exe CancelWindowDisplayAffinity.cpp payload.res user32.lib psapi.lib advapi32.lib shell32.lib >nul 2>&1"
if not exist CancelWindowDisplayAffinity.exe ( echo FAILED & exit /b 1 )
echo       OK

REM Cleanup intermediate files
del /q *.obj *.exp *.lib Affinity32.dll Affinity64.dll payload.res 2>nul
del /q CancelWindowDisplayAffinity.dll 2>nul

echo.
echo === Build successful! ===
echo Output: CancelWindowDisplayAffinity.exe (single file, ~30KB)
echo.
pause

@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

echo === CancelWindowDisplayAffinity (Hook Edition) Build ===
echo.

REM --- Find Visual Studio ---
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" ( echo [ERR] vswhere not found. & exit /b 1 )
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR ( echo [ERR] Visual Studio not found. & exit /b 1 )
set "VCVARS=%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat"
echo Found VS: %VSDIR%
echo.

REM --- Download MinHook if needed ---
if not exist minhook (
    echo [0/5] Downloading MinHook...
    powershell -Command "Invoke-WebRequest -Uri 'https://github.com/TsudaKageyu/minhook/archive/refs/heads/master.zip' -OutFile minhook.zip"
    if not exist minhook.zip ( echo [ERR] Download failed. & exit /b 1 )
    powershell -Command "Expand-Archive -Path minhook.zip -DestinationPath . -Force"
    if exist minhook-master ( rename minhook-master minhook )
    del /q minhook.zip 2>nul
    echo       OK
) else (
    echo [0/5] MinHook already present.
)
echo.

set "MH_SRC=minhook/src/hook.c minhook/src/buffer.c minhook/src/trampoline.c minhook/src/hde/hde32.c minhook/src/hde/hde64.c"

REM --- Step 1: Compile 32-bit hook DLL ---
echo [1/5] Building HookAffinity32.dll (x86)...
cmd /c ""%VCVARS%" x86 >nul 2>&1 && cl /nologo /LD /I minhook/include /Fe:HookAffinity32.dll hook_dll.cpp %MH_SRC% user32.lib >nul 2>&1"
if not exist HookAffinity32.dll ( echo       FAILED & exit /b 1 )
echo       OK

REM --- Step 2: Compile 64-bit hook DLL ---
echo [2/5] Building HookAffinity64.dll (x64)...
cmd /c ""%VCVARS%" x64 >nul 2>&1 && cl /nologo /LD /I minhook/include /Fe:HookAffinity64.dll hook_dll.cpp %MH_SRC% user32.lib >nul 2>&1"
if not exist HookAffinity64.dll ( echo       FAILED & exit /b 1 )
echo       OK

REM --- Step 3: Compile resource ---
echo [3/5] Compiling payload.rc...
cmd /c ""%VCVARS%" x64 >nul 2>&1 && rc /nologo payload.rc >nul 2>&1"
if not exist payload.res ( echo       FAILED & exit /b 1 )
echo       OK

REM --- Step 4: Compile 64-bit EXE ---
echo [4/5] Building CancelWindowDisplayAffinity.exe (x64)...
cmd /c ""%VCVARS%" x64 >nul 2>&1 && cl /nologo /EHsc /Fe:CancelWindowDisplayAffinity.exe main.cpp payload.res user32.lib psapi.lib advapi32.lib shell32.lib >nul 2>&1"
if not exist CancelWindowDisplayAffinity.exe ( echo       FAILED & exit /b 1 )
echo       OK

REM --- Step 5: Cleanup ---
echo [5/5] Cleanup...
del /q *.obj *.exp *.lib *.res 2>nul
del /q HookAffinity32.dll HookAffinity64.dll 2>nul
del /q hook_dll.dll 2>nul
echo       OK

echo.
echo === Build successful! ===
echo Output: CancelWindowDisplayAffinity.exe
echo.
echo Approach: Inline Hook (MinHook)
echo   - Hooks SetWindowDisplayAffinity: forces WDA_NONE
echo   - Hooks GetWindowDisplayAffinity: spoofs return value
echo   - Zero CPU polling, event-driven
echo.
pause

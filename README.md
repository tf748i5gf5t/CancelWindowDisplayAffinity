# Display Affinity Remover

A Windows utility to remove display affinity protections from windows that prevent screen capture.

## Description

This project consists of three main components:

1. **DLL Injection Module** (`dllmain.cpp`) - Monitors and removes display affinity settings from windows
2. **Injector Tool** (`injector.cpp`) - Injects the DLL into target processes
3. **Main Application** (`main.cpp`) - Finds processes with display affinity and coordinates the injection

## Features

- Detects processes using display affinity (WDA_MONITOR/WDA_EXCLUDEFROMCAPTURE)
- Automatically removes display affinity settings
- Supports both 32-bit and 64-bit processes
- Requires and automatically requests administrator privileges

## Build Instructions

### Requirements
- Windows SDK
- Visual Studio or compatible compiler

### Compilation

1. **DLL Module**:
   ```
   cl /nologo /EHsc /LD /Fe:Affinity32.dll dllmain.cpp user32.lib
   cl /nologo /EHsc /LD /Fe:Affinity64.dll dllmain.cpp user32.lib
   ```

2. **Injector Tool**:
   ```
   cl.exe /EHsc /nologo /Fe:injector.exe injector.cpp user32.lib psapi.lib
   cl.exe /EHsc /nologo /Fe:injector64.exe injector.cpp user32.lib psapi.lib
   ```

3. **Main Application**:
   ```
   cl.exe /EHsc /nologo /Fe:DisplayAffinityRemover.exe main.cpp user32.lib psapi.lib advapi32.lib shell32.lib
   ```

## Usage

1. Place all compiled executables and DLLs in the same directory
2. Run `CancelWindowDisplayAffinity.exe` as administrator (it will auto-elevate if needed)
3. The program will automatically:
   - Find all processes with display affinity
   - Inject the appropriate DLL (32-bit or 64-bit)
   - Remove display affinity protections

## Notes

- This tool is designed for legitimate screen capture purposes
- Some applications may detect and block DLL injection
- Use responsibly and in compliance with applicable laws and terms of service

## License

This project is provided as-is without warranty. Use at your own risk.

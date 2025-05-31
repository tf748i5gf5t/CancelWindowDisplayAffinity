#include <windows.h>
#include <thread>
#include <atomic>
DWORD g_currentProcessId = 0;
std::atomic<bool> g_running(true);
void MonitorDisplayAffinity() {
    while (g_running) {
        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            DWORD windowProcessId = 0;
            GetWindowThreadProcessId(hwnd, &windowProcessId);
            if (windowProcessId == g_currentProcessId) {
                DWORD pdwAffinity = 0;
                if (GetWindowDisplayAffinity(hwnd, &pdwAffinity)) {
                    if (pdwAffinity != WDA_NONE) {
                        SetWindowDisplayAffinity(hwnd, WDA_NONE);
                    }
                }
            }
            return TRUE;
        }, 0);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}
BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        g_currentProcessId = GetCurrentProcessId();
        DisableThreadLibraryCalls(hModule); 
        std::thread(MonitorDisplayAffinity).detach(); 
        break;
    case DLL_PROCESS_DETACH:
        g_running = false; 
        break;
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}
// cl /nologo /EHsc /LD /Fe:test.dll dllmain.cpp user32.lib   

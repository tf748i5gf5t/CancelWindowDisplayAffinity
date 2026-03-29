/*
 * Hook DLL — Inline hooks SetWindowDisplayAffinity & GetWindowDisplayAffinity
 * using MinHook. Zero polling, event-driven, stealthy.
 *
 * Build: cl /nologo /LD /I minhook/include /Fe:HookAffinity64.dll hook_dll.cpp
 *        minhook/src/hook.c minhook/src/buffer.c minhook/src/trampoline.c
 *        minhook/src/hde/hde32.c minhook/src/hde/hde64.c user32.lib
 */
#include <windows.h>
#include "MinHook.h"
#pragma comment(lib, "user32.lib")

/* --- Original function trampolines --- */
typedef BOOL(WINAPI* PfnSetWDA)(HWND, DWORD);
typedef BOOL(WINAPI* PfnGetWDA)(HWND, DWORD*);
static PfnSetWDA oSetWDA = NULL;
static PfnGetWDA oGetWDA = NULL;

/* Spoofed affinity value (what the app thinks is set) */
static volatile DWORD g_spoof = WDA_NONE;

/* --- Hook: SetWindowDisplayAffinity --- */
BOOL WINAPI hkSetWDA(HWND hwnd, DWORD dwAffinity) {
    /* Record what the app wanted, but always set WDA_NONE */
    if (dwAffinity != WDA_NONE)
        InterlockedExchange((LONG*)&g_spoof, (LONG)dwAffinity);
    return oSetWDA(hwnd, WDA_NONE);
}

/* --- Hook: GetWindowDisplayAffinity --- */
BOOL WINAPI hkGetWDA(HWND hwnd, DWORD* pdwAffinity) {
    BOOL ret = oGetWDA(hwnd, pdwAffinity);
    /* Return the spoofed value so the app thinks protection is still active */
    if (ret && pdwAffinity && g_spoof != WDA_NONE) {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == GetCurrentProcessId())
            *pdwAffinity = g_spoof;
    }
    return ret;
}

/* --- Reset all existing affinity on current process windows --- */
static void ResetExistingAffinity(void) {
    DWORD myPid = GetCurrentProcessId();
    EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (pid == (DWORD)lParam && oGetWDA && oSetWDA) {
            DWORD aff = 0;
            if (oGetWDA(hwnd, &aff) && aff != WDA_NONE) {
                InterlockedExchange((LONG*)&g_spoof, (LONG)aff);
                oSetWDA(hwnd, WDA_NONE);
            }
        }
        return TRUE;
    }, (LPARAM)myPid);
}

/* --- Install / remove hooks --- */
static BOOL InstallHooks(void) {
    if (MH_Initialize() != MH_OK)
        return FALSE;

    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    if (!hUser32) return FALSE;

    void* pSet = (void*)GetProcAddress(hUser32, "SetWindowDisplayAffinity");
    void* pGet = (void*)GetProcAddress(hUser32, "GetWindowDisplayAffinity");

    if (pSet && MH_CreateHook(pSet, (void*)hkSetWDA, (void**)&oSetWDA) != MH_OK)
        return FALSE;
    if (pGet && MH_CreateHook(pGet, (void*)hkGetWDA, (void**)&oGetWDA) != MH_OK)
        return FALSE;

    return MH_EnableHook(MH_ALL_HOOKS) == MH_OK;
}

static void RemoveHooks(void) {
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
}

/* --- DLL entry point --- */
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    (void)lpReserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        if (InstallHooks())
            ResetExistingAffinity();
    } else if (reason == DLL_PROCESS_DETACH) {
        RemoveHooks();
    }
    return TRUE;
}

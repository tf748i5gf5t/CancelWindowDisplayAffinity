/*
 * CancelWindowDisplayAffinity - Single Binary Solution
 *
 * BUILD_DLL  -> monitoring DLL (compiled for both x86 and x64)
 * default    -> 64-bit EXE with embedded DLL payloads + cross-arch injection
 *
 * Build with build.bat (see README.md)
 */

#ifdef BUILD_DLL
/* ==============================================================
 *  DLL MODE - injected into target, resets display affinity
 * ============================================================== */
#include <windows.h>

static DWORD g_pid = 0;
static volatile LONG g_run = TRUE;
static HANDLE g_hThread = NULL;

static DWORD WINAPI MonitorProc(LPVOID p) {
    (void)p;
    while (InterlockedCompareExchange(&g_run, TRUE, TRUE)) {
        EnumWindows([](HWND hwnd, LPARAM lp) -> BOOL {
            (void)lp;
            DWORD wpid = 0;
            GetWindowThreadProcessId(hwnd, &wpid);
            if (wpid == g_pid) {
                DWORD aff = 0;
                if (GetWindowDisplayAffinity(hwnd, &aff) && aff != WDA_NONE)
                    SetWindowDisplayAffinity(hwnd, WDA_NONE);
            }
            return TRUE;
        }, 0);
        Sleep(500);
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_pid = GetCurrentProcessId();
        DisableThreadLibraryCalls(hMod);
        g_hThread = CreateThread(NULL, 0, MonitorProc, NULL, 0, NULL);
    } else if (reason == DLL_PROCESS_DETACH) {
        InterlockedExchange(&g_run, FALSE);
        if (g_hThread) { WaitForSingleObject(g_hThread, 3000); CloseHandle(g_hThread); }
    }
    return TRUE;
}

#else /* EXE MODE */
/* ==============================================================
 *  EXE MODE - scanner + injector with embedded DLL payloads
 *  Cross-arch (64->32) injection via PE export table walking
 * ============================================================== */
#undef UNICODE
#undef _UNICODE
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <psapi.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include "resource.h"

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")

#ifndef _WIN64
#pragma message("WARNING: 32-bit build cannot inject 64-bit targets. Compile as x64 for full support.")
#endif

/* ---------- Utility ---------- */

static void PrintErr(const char* fn) {
    DWORD e = GetLastError();
    char* buf = NULL;
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                   FORMAT_MESSAGE_IGNORE_INSERTS, NULL, e,
                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&buf, 0, NULL);
    if (buf) { printf("  [ERR] %s (%d): %s", fn, e, buf); LocalFree(buf); }
    else     { printf("  [ERR] %s (%d)\n", fn, e); }
}

static BOOL IsAdmin(void) {
    BOOL ok = FALSE; PSID sid = NULL;
    SID_IDENTIFIER_AUTHORITY auth = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&auth, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0,0,0,0,0,0, &sid))
    { CheckTokenMembership(NULL, sid, &ok); FreeSid(sid); }
    return ok;
}

static BOOL Is64(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!h) return FALSE;
    BOOL wow = FALSE;
    BOOL r = IsWow64Process(h, &wow);
    CloseHandle(h);
    return r ? !wow : FALSE;
}

/* ---------- Extract embedded DLL to temp ---------- */

static bool ExtractDLL(int resId, const char* outPath) {
    HRSRC hr = FindResource(NULL, MAKEINTRESOURCE(resId), RT_RCDATA);
    if (!hr) { printf("  [ERR] Resource %d not found.\n", resId); return false; }
    HGLOBAL hg = LoadResource(NULL, hr);
    DWORD sz = SizeofResource(NULL, hr);
    void* p = LockResource(hg);
    if (!p || !sz) return false;
    HANDLE hf = CreateFileA(outPath, GENERIC_WRITE, 0, NULL,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hf == INVALID_HANDLE_VALUE) { PrintErr("CreateFile(extract)"); return false; }
    DWORD w = 0;
    WriteFile(hf, p, sz, &w, NULL);
    CloseHandle(hf);
    return w == sz;
}

/* ---------- Same-arch injection ---------- */

static bool InjectSameArch(DWORD pid, const char* dll) {
    HANDLE hp = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hp) { PrintErr("OpenProcess"); return false; }

    size_t len = strlen(dll) + 1;
    LPVOID rm = VirtualAllocEx(hp, NULL, len, MEM_COMMIT, PAGE_READWRITE);
    if (!rm) { PrintErr("VirtualAllocEx"); CloseHandle(hp); return false; }
    if (!WriteProcessMemory(hp, rm, dll, len, NULL)) {
        PrintErr("WriteProcessMemory");
        VirtualFreeEx(hp, rm, 0, MEM_RELEASE); CloseHandle(hp); return false;
    }

    LPTHREAD_START_ROUTINE pLL = (LPTHREAD_START_ROUTINE)
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    if (!pLL) { VirtualFreeEx(hp, rm, 0, MEM_RELEASE); CloseHandle(hp); return false; }

    HANDLE ht = CreateRemoteThread(hp, NULL, 0, pLL, rm, 0, NULL);
    if (!ht) { PrintErr("CreateRemoteThread");
        VirtualFreeEx(hp, rm, 0, MEM_RELEASE); CloseHandle(hp); return false; }
    WaitForSingleObject(ht, INFINITE);
    DWORD ec = 0; GetExitCodeThread(ht, &ec);
    VirtualFreeEx(hp, rm, 0, MEM_RELEASE);
    CloseHandle(ht); CloseHandle(hp);
    if (!ec) { printf("  [WARN] LoadLibraryA returned NULL.\n"); return false; }
    return true;
}

/* ---------- Cross-arch injection (64-bit EXE -> 32-bit target) ---------- */

#ifdef _WIN64
static LPTHREAD_START_ROUTINE Resolve32LoadLibA(HANDLE hp, DWORD pid) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) { PrintErr("Toolhelp32Snapshot"); return NULL; }

    MODULEENTRY32 me; me.dwSize = sizeof(me);
    BYTE* base = NULL;
    if (Module32First(snap, &me)) {
        do { if (_stricmp(me.szModule, "kernel32.dll") == 0)
            { base = me.modBaseAddr; break; }
        } while (Module32Next(snap, &me));
    }
    CloseHandle(snap);
    if (!base) { printf("  [ERR] kernel32.dll not found in target.\n"); return NULL; }

    IMAGE_DOS_HEADER dosh;
    if (!ReadProcessMemory(hp, base, &dosh, sizeof(dosh), NULL) ||
         dosh.e_magic != IMAGE_DOS_SIGNATURE) return NULL;

    IMAGE_NT_HEADERS32 nth;
    if (!ReadProcessMemory(hp, base + dosh.e_lfanew, &nth, sizeof(nth), NULL) ||
         nth.Signature != IMAGE_NT_SIGNATURE) return NULL;

    DWORD expRVA = nth.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    if (!expRVA) return NULL;

    IMAGE_EXPORT_DIRECTORY ed;
    if (!ReadProcessMemory(hp, base + expRVA, &ed, sizeof(ed), NULL)) return NULL;

    DWORD nn = ed.NumberOfNames, nf = ed.NumberOfFunctions;
    DWORD* nrva = (DWORD*)HeapAlloc(GetProcessHeap(), 0, nn*4);
    WORD*  ords = (WORD*) HeapAlloc(GetProcessHeap(), 0, nn*2);
    DWORD* frva = (DWORD*)HeapAlloc(GetProcessHeap(), 0, nf*4);
    if (!nrva||!ords||!frva) goto fail;

    ReadProcessMemory(hp, base+ed.AddressOfNames,        nrva, nn*4, NULL);
    ReadProcessMemory(hp, base+ed.AddressOfNameOrdinals, ords, nn*2, NULL);
    ReadProcessMemory(hp, base+ed.AddressOfFunctions,    frva, nf*4, NULL);

    LPTHREAD_START_ROUTINE result = NULL;
    for (DWORD i = 0; i < nn; i++) {
        char name[32] = {0};
        ReadProcessMemory(hp, base + nrva[i], name, 31, NULL);
        if (strcmp(name, "LoadLibraryA") == 0 && ords[i] < nf) {
            result = (LPTHREAD_START_ROUTINE)(base + frva[ords[i]]);
            break;
        }
    }
fail:
    if (nrva) HeapFree(GetProcessHeap(), 0, nrva);
    if (ords) HeapFree(GetProcessHeap(), 0, ords);
    if (frva) HeapFree(GetProcessHeap(), 0, frva);
    return result;
}

static bool InjectCrossArch32(DWORD pid, const char* dll) {
    HANDLE hp = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hp) { PrintErr("OpenProcess"); return false; }

    LPTHREAD_START_ROUTINE pLL = Resolve32LoadLibA(hp, pid);
    if (!pLL) { printf("  [ERR] Cannot resolve 32-bit LoadLibraryA.\n");
        CloseHandle(hp); return false; }

    size_t len = strlen(dll) + 1;
    LPVOID rm = VirtualAllocEx(hp, NULL, len, MEM_COMMIT, PAGE_READWRITE);
    if (!rm) { PrintErr("VirtualAllocEx"); CloseHandle(hp); return false; }
    if (!WriteProcessMemory(hp, rm, dll, len, NULL)) {
        PrintErr("WriteProcessMemory");
        VirtualFreeEx(hp, rm, 0, MEM_RELEASE); CloseHandle(hp); return false;
    }

    HANDLE ht = CreateRemoteThread(hp, NULL, 0, pLL, rm, 0, NULL);
    if (!ht) { PrintErr("CreateRemoteThread");
        VirtualFreeEx(hp, rm, 0, MEM_RELEASE); CloseHandle(hp); return false; }
    WaitForSingleObject(ht, INFINITE);
    DWORD ec = 0; GetExitCodeThread(ht, &ec);
    VirtualFreeEx(hp, rm, 0, MEM_RELEASE);
    CloseHandle(ht); CloseHandle(hp);
    if (!ec) { printf("  [WARN] LoadLibraryA returned NULL.\n"); return false; }
    return true;
}
#endif /* _WIN64 */

/* ---------- Window enumeration ---------- */

struct EnumCtx { DWORD pids[256]; int n; };

static BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lp) {
    EnumCtx* c = (EnumCtx*)lp;
    DWORD aff = 0;
    GetWindowDisplayAffinity(hwnd, &aff);
    if (!aff) return TRUE;
    DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) return TRUE;
    for (int i = 0; i < c->n; i++) if (c->pids[i] == pid) return TRUE;
    if (c->n < 256) c->pids[c->n++] = pid;
    return TRUE;
}

/* ---------- Entry ---------- */

int main(void) {
    if (!IsAdmin()) {
        char ep[MAX_PATH]={0}; GetModuleFileNameA(NULL, ep, MAX_PATH);
        ShellExecuteA(NULL, "runas", ep, NULL, NULL, SW_SHOWNORMAL);
        return 0;
    }

    printf("=== CancelWindowDisplayAffinity ===\n");
#ifdef _WIN64
    printf("64-bit build — supports both 32-bit and 64-bit targets\n\n");
#else
    printf("32-bit build — supports 32-bit targets only\n\n");
#endif

    /* Extract DLL payloads to temp directory */
    char tmp[MAX_PATH]; GetTempPathA(MAX_PATH, tmp);
    char dll32[MAX_PATH], dll64[MAX_PATH];
    snprintf(dll32, MAX_PATH, "%sAffinity32.dll", tmp);
    snprintf(dll64, MAX_PATH, "%sAffinity64.dll", tmp);

    bool have32 = ExtractDLL(IDR_AFFINITY32_DLL, dll32);
    bool have64 = ExtractDLL(IDR_AFFINITY64_DLL, dll64);
    if (!have32 && !have64) {
        printf("[ERR] Failed to extract DLL payloads.\n");
        system("pause"); return 1;
    }

    /* Enumerate */
    EnumCtx ctx; ZeroMemory(&ctx, sizeof(ctx));
    EnumWindows(EnumProc, (LPARAM)&ctx);
    if (!ctx.n) {
        printf("No windows with display affinity found.\n");
        DeleteFileA(dll32); DeleteFileA(dll64);
        system("pause"); return 1;
    }
    printf("Found %d process(es) with display affinity.\n\n", ctx.n);

    for (int i = 0; i < ctx.n; i++) {
        DWORD pid = ctx.pids[i];
        char pp[MAX_PATH] = "<unknown>";
        HANDLE hp = OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ, FALSE, pid);
        if (hp) { GetModuleFileNameExA(hp, NULL, pp, MAX_PATH); CloseHandle(hp); }

        BOOL t64 = Is64(pid);
        printf("[%d/%d] PID %u  (%s)\n", i+1, ctx.n, (unsigned)pid, t64?"64-bit":"32-bit");
        printf("  Path: %s\n", pp);

        bool ok = false;
#ifdef _WIN64
        if (t64) {
            printf("  Method: direct (same arch)\n");
            ok = have64 && InjectSameArch(pid, dll64);
        } else {
            printf("  Method: cross-arch PE walking\n");
            ok = have32 && InjectCrossArch32(pid, dll32);
        }
#else
        if (!t64) {
            printf("  Method: direct (same arch)\n");
            ok = have32 && InjectSameArch(pid, dll32);
        } else {
            printf("  [SKIP] Cannot inject 64-bit from 32-bit build.\n");
        }
#endif
        printf("  Result: %s\n\n", ok ? "SUCCESS" : "FAILED");
    }

    /* Cleanup temp (best effort, may fail if DLL is in use) */
    DeleteFileA(dll32); DeleteFileA(dll64);
    system("pause");
    return 0;
}
#endif /* BUILD_DLL */

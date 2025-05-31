#undef UNICODE
#undef _UNICODE
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <psapi.h>
#include <string>
#include <vector>
#include <shellapi.h>
#include <algorithm>
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "advapi32.lib")   
#pragma comment(lib, "shell32.lib") 



std::vector<DWORD> Affinity_pidList;
BOOL IsRunAsAdmin()
{
    BOOL fIsRunAsAdmin = FALSE;
    PSID pAdministratorsGroup = NULL;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&NtAuthority, 2,
        SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
        0, 0, 0, 0, 0, 0, &pAdministratorsGroup))
    {
        CheckTokenMembership(NULL, pAdministratorsGroup, &fIsRunAsAdmin);
        FreeSid(pAdministratorsGroup);
    }
    return fIsRunAsAdmin;
}

BOOL IsProcess64Bit(DWORD pid) {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProcess) return FALSE;
    
    BOOL isWow64 = FALSE;
    if (!IsWow64Process(hProcess, &isWow64)) {
        CloseHandle(hProcess);
        return FALSE;
    }
    CloseHandle(hProcess);
    return !isWow64;
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    DWORD pdwAffinity = 0;
    GetWindowDisplayAffinity(hwnd, &pdwAffinity);
    if (!pdwAffinity) return TRUE; 
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid) Affinity_pidList.push_back(pid);
    return TRUE;
}


int main() {
    if (!IsRunAsAdmin()) {
        char exePath[MAX_PATH] = {0};
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        ShellExecuteA(NULL, "runas", exePath, NULL, NULL, SW_SHOWNORMAL);
        return 0;
    }
    char exePath[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    char* lastSlash = strrchr(exePath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    char dllPath32[MAX_PATH] = {0};
    char dllPath64[MAX_PATH] = {0};
    snprintf(dllPath32, MAX_PATH, "%sAffinity32.dll", exePath);
    snprintf(dllPath64, MAX_PATH, "%sAffinity64.dll", exePath);
    EnumWindows(EnumWindowsProc, 0);
    std::sort(Affinity_pidList.begin(), Affinity_pidList.end());
    Affinity_pidList.erase(std::unique(Affinity_pidList.begin(), Affinity_pidList.end()), Affinity_pidList.end());
    if (Affinity_pidList.empty()) {
        printf("No windows with display affinity found.\n");
        system("pause");
        return 1;
    }
    for (DWORD pid : Affinity_pidList) {
        char processPath[MAX_PATH] = "<unknown>";
        HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (hProcess) {
            GetModuleFileNameExA(hProcess, NULL, processPath, sizeof(processPath));
            CloseHandle(hProcess);
        }
        printf("PID: %d \nProcess Path: %s\n", pid, processPath);
        printf("Injecting DLL into process '%d'...\n", pid);
        BOOL is64bit = IsProcess64Bit(pid);
        BOOL result = FALSE;
        char cmd[MAX_PATH * 3] = {0};
        if (is64bit) {
            printf("Target process is 64-bit.\n");
            snprintf(cmd, sizeof(cmd), "injector64.exe %d %s", pid, dllPath64);
            system(cmd);
        } else {
            printf("Target process is 32-bit.\n");
            snprintf(cmd, sizeof(cmd), "injector.exe %d %s", pid, dllPath32);
            system(cmd);
        }
    }
    system("pause");
    return 0;
}
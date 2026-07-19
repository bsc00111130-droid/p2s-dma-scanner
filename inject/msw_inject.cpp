#define UNICODE
#define _UNICODE
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#pragma comment(lib, "advapi32.lib")

void EnableDebug() {
    HANDLE hToken; TOKEN_PRIVILEGES tp;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY, &hToken)) return;
    LookupPrivilegeValueW(NULL, L"SeDebugPrivilege", &tp.Privileges[0].Luid);
    tp.PrivilegeCount = 1; tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, NULL);
    CloseHandle(hToken);
}

DWORD FindMSW() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(snap, &pe)) do {
        if (_wcsicmp(pe.szExeFile, L"msw.exe") == 0) { CloseHandle(snap); return pe.th32ProcessID; }
    } while (Process32NextW(snap, &pe));
    CloseHandle(snap); return 0;
}

bool InjectDLL(DWORD pid, const wchar_t* path) {
    HANDLE h = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!h) return false;
    size_t sz = (wcslen(path) + 1) * sizeof(wchar_t);
    void* mem = VirtualAllocEx(h, NULL, sz, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    if (!mem) { CloseHandle(h); return false; }
    WriteProcessMemory(h, mem, path, sz, NULL);
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    LPTHREAD_START_ROUTINE ll = (LPTHREAD_START_ROUTINE)GetProcAddress(k32, "LoadLibraryW");
    HANDLE thr = CreateRemoteThread(h, NULL, 0, ll, mem, 0, NULL);
    if (!thr) { VirtualFreeEx(h, mem, 0, MEM_RELEASE); CloseHandle(h); return false; }
    WaitForSingleObject(thr, 5000);
    CloseHandle(thr); CloseHandle(h);
    return true;
}

int wmain() {
    EnableDebug();
    DWORD pid = FindMSW();
    if (!pid) { wprintf(L"target not found\n"); return 1; }
    wprintf(L"PID=%d\n", pid);
    const wchar_t* dlls[] = { L"net_hook.dll", L"output_filter.dll", L"MSWorld.dll", L"planet_inject.dll" };
    wchar_t dir[260]; GetCurrentDirectoryW(260, dir);
    for (int i = 0; i < 4; i++) {
        wchar_t path[520]; wcscpy_s(path, dir); wcscat_s(path, L"\\"); wcscat_s(path, dlls[i]);
        wprintf(L"  %ls... ", dlls[i]);
        if (InjectDLL(pid, path)) wprintf(L"OK\n"); else wprintf(L"FAIL (%d)\n", GetLastError());
        Sleep(1000);
    }
    return 0;
}

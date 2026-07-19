#define UNICODE
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

bool InjectAPC(DWORD pid, const wchar_t* dllPath) {
    HANDLE h = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!h) { wprintf(L"OpenProcess err=%d ", GetLastError()); return false; }
    size_t sz = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    void* mem = VirtualAllocEx(h, NULL, sz, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
    if (!mem) { wprintf(L"VirtualAllocEx err=%d ", GetLastError()); CloseHandle(h); return false; }
    WriteProcessMemory(h, mem, dllPath, sz, NULL);
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    PTHREAD_START_ROUTINE ll = (PTHREAD_START_ROUTINE)GetProcAddress(k32, "LoadLibraryW");
    HANDLE th = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 te = { sizeof(te) };
    int count = 0;
    if (Thread32First(th, &te)) do {
        if (te.th32OwnerProcessID == pid) {
            HANDLE t = OpenThread(THREAD_SET_CONTEXT, FALSE, te.th32ThreadID);
            if (t) { QueueUserAPC((PAPCFUNC)ll, t, (ULONG_PTR)mem); CloseHandle(t); count++; }
        }
    } while (Thread32Next(th, &te));
    CloseHandle(th);
    if (count == 0) { VirtualFreeEx(h, mem, 0, MEM_RELEASE); CloseHandle(h); return false; }
    CloseHandle(h);
    return true;
}

int wmain() {
    EnableDebug();
    DWORD pid = FindMSW();
    if (!pid) { wprintf(L"target not found\n"); return 1; }
    wprintf(L"PID=%d\n", pid);
    const wchar_t* dlls[] = { L"net_hook.dll", L"output_filter.dll", L"MSWorld.dll", L"planet_inject.dll" };
    wchar_t dir[260]; GetCurrentDirectoryW(260, dir);
    int ok = 0, fail = 0;
    for (int i = 0; i < 4; i++) {
        wchar_t path[520]; wcscpy_s(path, dir); wcscat_s(path, L"\\"); wcscat_s(path, dlls[i]);
        wprintf(L"  %ls... ", dlls[i]);
        if (InjectAPC(pid, path)) { wprintf(L"OK\n"); ok++; }
        else { wprintf(L"FAIL (%d)\n", GetLastError()); fail++; }
        Sleep(1500);
    }
    wprintf(L"\n[%d OK, %d FAIL]\n", ok, fail);
    return fail > 0 ? 1 : 0;
}

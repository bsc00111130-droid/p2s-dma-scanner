# -*- coding: utf-8 -*-
""" MSWloader with SeDebugPrivilege — required for WriteProcessMemory on protected processes """
import ctypes, os, sys, time, secrets
from ctypes import wintypes

# ─── Enable SeDebugPrivilege ────────────────────────────────
def enable_debug():
    hToken = ctypes.c_void_p()
    TOKEN_ADJUST_PRIVILEGES = 0x0020
    TOKEN_QUERY = 0x0008
    SE_PRIVILEGE_ENABLED = 0x02

    ctypes.windll.advapi32.OpenProcessToken(
        ctypes.windll.kernel32.GetCurrentProcess(),
        TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY,
        ctypes.byref(hToken))

    class LUID(ctypes.Structure):
        _fields_ = [("LowPart", wintypes.DWORD), ("HighPart", wintypes.LONG)]

    class TOKEN_PRIVILEGES(ctypes.Structure):
        _fields_ = [("PrivilegeCount", wintypes.DWORD),
                    ("Luid", LUID),
                    ("Attributes", wintypes.DWORD)]

    luid = LUID()
    ctypes.windll.advapi32.LookupPrivilegeValueW(None, "SeDebugPrivilege", ctypes.byref(luid))
    tp = TOKEN_PRIVILEGES()
    tp.PrivilegeCount = 1
    tp.Luid = luid
    tp.Attributes = SE_PRIVILEGE_ENABLED

    ctypes.windll.advapi32.AdjustTokenPrivileges(hToken, False, ctypes.byref(tp), 0, None, None)
    return True

enable_debug()
print("[OK] SeDebugPrivilege enabled")

# ─── DLL Injection ──────────────────────────────────────────
k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
k32.OpenProcess.restype = wintypes.HANDLE
k32.VirtualAllocEx.argtypes = [wintypes.HANDLE, wintypes.LPVOID, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD]
k32.VirtualAllocEx.restype = wintypes.LPVOID
k32.WriteProcessMemory.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.LPCVOID, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
k32.WriteProcessMemory.restype = wintypes.BOOL
k32.CreateRemoteThread.argtypes = [wintypes.HANDLE, wintypes.LPVOID, ctypes.c_size_t, wintypes.LPVOID, wintypes.LPVOID, wintypes.DWORD, ctypes.POINTER(wintypes.DWORD)]
k32.CreateRemoteThread.restype = wintypes.HANDLE
k32.CloseHandle.argtypes = [wintypes.HANDLE]
k32.CloseHandle.restype = wintypes.BOOL
k32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
k32.WaitForSingleObject.restype = wintypes.DWORD
k32.GetModuleHandleW.argtypes = [wintypes.LPCWSTR]
k32.GetModuleHandleW.restype = wintypes.HMODULE
k32.GetProcAddress.argtypes = [wintypes.HMODULE, wintypes.LPCSTR]
k32.GetProcAddress.restype = wintypes.LPVOID

BASE = os.path.dirname(os.path.abspath(__file__))

def find_msw():
    import psutil
    for p in psutil.process_iter(['pid', 'name']):
        try:
            n = p.info['name'] or ''
            if 'msw' in n.lower() or 'MapleStory' in n:
                return p.info['pid']
        except: pass
    return 0

def inject(pid, dll_path):
    full = os.path.abspath(dll_path)
    if not os.path.exists(full): return False, "file not found"
    h = k32.OpenProcess(0x1F0FFF, False, pid)
    if not h: return False, f"OpenProcess err={ctypes.get_last_error()}"
    buf = ctypes.create_unicode_buffer(full)
    bs = ctypes.sizeof(buf)
    addr = k32.VirtualAllocEx(h, None, bs, 0x3000, 0x04)
    if not addr: k32.CloseHandle(h); return False, f"VirtualAllocEx err={ctypes.get_last_error()}"
    wr = ctypes.c_size_t(0)
    if not k32.WriteProcessMemory(h, addr, buf, bs, ctypes.byref(wr)):
        err = ctypes.get_last_error()
        k32.VirtualFreeEx(h, addr, 0, 0x8000); k32.CloseHandle(h)
        return False, f"WriteProcessMemory err={err}"
    km = k32.GetModuleHandleW("kernel32.dll")
    ll = k32.GetProcAddress(km, b"LoadLibraryW")
    tid = wintypes.DWORD(0)
    thr = k32.CreateRemoteThread(h, None, 0, ll, addr, 0, ctypes.byref(tid))
    if not thr:
        err = ctypes.get_last_error()
        k32.VirtualFreeEx(h, addr, 0, 0x8000); k32.CloseHandle(h)
        return False, f"CreateRemoteThread err={err}"
    k32.WaitForSingleObject(thr, 10000)
    k32.CloseHandle(thr); k32.CloseHandle(h)
    return True, "OK"

def main():
    pid = find_msw()
    if not pid:
        print("[!] msw.exe not found")
        return
    print(f"[OK] msw.exe PID: {pid}")

    order = ["net_hook.dll", "output_filter.dll", "MSWorld.dll", "planet_inject.dll"]
    for dll in order:
        path = os.path.join(BASE, dll)
        if not os.path.exists(path):
            print(f"  [!] {dll} missing")
            continue
        print(f"  Injecting {dll}...", end=" ")
        ok, msg = inject(pid, path)
        print(f"[{'OK' if ok else 'FAIL'}] {msg}")
        time.sleep(1)
    print("\n[Done]")

if __name__ == "__main__":
    main()

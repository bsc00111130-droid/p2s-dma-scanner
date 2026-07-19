# -*- coding: utf-8 -*-
""" MSWloader - License bypass + inject """
import ctypes, os, sys, time, secrets, struct
from ctypes import wintypes

BASE = os.path.dirname(os.path.abspath(__file__))
k32 = ctypes.WinDLL("kernel32", use_last_error=True)

# Set correct 64-bit function signatures
k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
k32.OpenProcess.restype = wintypes.HANDLE
k32.VirtualAllocEx.argtypes = [wintypes.HANDLE, wintypes.LPVOID, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD]
k32.VirtualAllocEx.restype = wintypes.LPVOID
k32.VirtualFreeEx.argtypes = [wintypes.HANDLE, wintypes.LPVOID, ctypes.c_size_t, wintypes.DWORD]
k32.VirtualFreeEx.restype = wintypes.BOOL
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

ACCESS_INJECT = 0x1F0FFF
MEM_COMMIT = 0x1000
MEM_RESERVE = 0x2000
PAGE_READWRITE = 0x04

def find_msw_pid():
    import subprocess
    try:
        out = subprocess.check_output(
            ["tasklist", "/FI", "IMAGENAME eq msw.exe", "/FO", "CSV", "/NH"],
            creationflags=0x08000000, timeout=5).decode("cp949", errors="replace")
        for line in out.splitlines():
            if "msw.exe" in line.lower():
                parts = line.split(",")
                if len(parts) >= 2:
                    pid = parts[1].strip().strip('"')
                    if pid.isdigit():
                        return int(pid)
    except: pass
    return 0

def get_load_library_w():
    km = k32.GetModuleHandleW("kernel32.dll")
    if not km:
        print(f"  [!] GetModuleHandleW error: {ctypes.get_last_error()}")
        return None
    addr = k32.GetProcAddress(km, b"LoadLibraryW")
    if not addr:
        print(f"  [!] GetProcAddress(LoadLibraryW) error: {ctypes.get_last_error()}")
    return addr

def inject(pid, dll_path):
    loadlib = get_load_library_w()
    if not loadlib:
        print(f"  [!] GetProcAddress failed")
        return False
    h = k32.OpenProcess(ACCESS_INJECT, False, pid)
    if not h:
        print(f"  [!] OpenProcess error: {ctypes.get_last_error()}")
        return False
    buf = ctypes.create_unicode_buffer(os.path.abspath(dll_path))
    buf_size = ctypes.sizeof(buf)
    addr = k32.VirtualAllocEx(h, None, buf_size, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE)
    if not addr:
        print(f"  [!] VirtualAllocEx error: {ctypes.get_last_error()}")
        k32.CloseHandle(h)
        return False
    written = ctypes.c_size_t(0)
    ok = k32.WriteProcessMemory(h, addr, buf, buf_size, ctypes.byref(written))
    if not ok:
        err = ctypes.get_last_error()
        print(f"  [!] WriteProcessMemory error: {err}")
        k32.VirtualFreeEx(h, addr, 0, 0x8000)
        k32.CloseHandle(h)
        return False
    tid = wintypes.DWORD(0)
    thr = k32.CreateRemoteThread(h, None, 0, loadlib, addr, 0, ctypes.byref(tid))
    if not thr:
        err = ctypes.get_last_error()
        print(f"  [!] CreateRemoteThread error: {err}")
        k32.VirtualFreeEx(h, addr, 0, 0x8000)
        k32.CloseHandle(h)
        return False
    k32.WaitForSingleObject(thr, 10000)
    k32.CloseHandle(thr)
    k32.CloseHandle(h)
    return True

def write_temp_dll(data):
    name = f"_msw_{secrets.token_hex(6)}.dll"
    path = os.path.join(os.environ.get("TEMP", "C:\\Windows\\Temp"), name)
    with open(path, "wb") as f:
        f.write(data)
    return path

def main():
    print("=== MSWloader (License Bypass) ===\n")
    dlls = ["net_hook.dll", "output_filter.dll", "MSWorld.dll", "planet_inject.dll"]
    dll_data = {}
    for d in dlls:
        p = os.path.join(BASE, d)
        if os.path.exists(p):
            dll_data[d] = open(p, "rb").read()
            print(f"  [OK] {d} ({len(dll_data[d])//1024}KB)")
        else:
            print(f"  [!] MISSING: {d}")
            return

    pid = find_msw_pid()
    if not pid:
        print("\n[!] msw.exe not running. Start MapleStory Worlds first.")
        return
    print(f"\n[OK] msw.exe PID: {pid}")

    # Phase 1: net_hook.dll
    print("\n[1/4] Injecting net_hook.dll...")
    p = write_temp_dll(dll_data["net_hook.dll"])
    if inject(pid, p):
        print("  [OK]")
    os.remove(p)

    # Phase 2: output_filter.dll
    print("[2/4] Injecting output_filter.dll...")
    p = write_temp_dll(dll_data["output_filter.dll"])
    if inject(pid, p):
        print("  [OK]")
    os.remove(p)

    # Phase 3: MSWorld.dll
    print("[3/4] Injecting MSWorld.dll...")
    p = write_temp_dll(dll_data["MSWorld.dll"])
    if inject(pid, p):
        print("  [OK]")
    os.remove(p)

    time.sleep(2)

    # Verify msw.exe is still running
    new_pid = find_msw_pid()
    if new_pid != pid:
        print(f"\n[!] msw.exe PID changed ({pid} -> {new_pid})")
        return

    # Phase 4: planet_inject.dll
    print("[4/4] Injecting planet_inject.dll...")
    p = write_temp_dll(dll_data["planet_inject.dll"])
    if inject(pid, p):
        print("  [OK]")
    os.remove(p)

    print("\n[Done] All DLLs injected. Check MapleStory Worlds.")

if __name__ == "__main__":
    main()

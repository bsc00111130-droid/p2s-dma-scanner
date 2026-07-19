# -*- coding: utf-8 -*-
""" MSWloader - Simplified launcher """
import ctypes, os, sys, time, struct

k32 = ctypes.windll.kernel32
BASE = os.path.dirname(os.path.abspath(__file__))
DLL_DIR = BASE

def find_maple():
    import psutil
    for p in psutil.process_iter(['pid', 'name']):
        try:
            n = p.info['name'] or ''
            if 'MapleStory' in n or n.lower() == 'msw.exe' or 'MapleStor' in n:
                return p.info['pid']
        except:
            pass
    return 0

def inject_dll(pid, dll_path):
    if not os.path.exists(dll_path):
        print(f"  [!] Missing: {dll_path}")
        return False
    hProcess = k32.OpenProcess(0x1F0FFF, False, pid)
    if not hProcess:
        print(f"  [!] OpenProcess error: {ctypes.GetLastError()}")
        return False
    dll_bytes = (dll_path + "\x00").encode('utf-16-le')
    addr = k32.VirtualAllocEx(hProcess, None, len(dll_bytes), 0x3000, 4)
    if not addr:
        print(f"  [!] VirtualAllocEx error: {ctypes.GetLastError()}")
        k32.CloseHandle(hProcess)
        return False
    written = ctypes.c_size_t(0)
    if not k32.WriteProcessMemory(hProcess, addr, dll_bytes, len(dll_bytes), ctypes.byref(written)):
        print(f"  [!] WriteProcessMemory error: {ctypes.GetLastError()}")
        k32.VirtualFreeEx(hProcess, addr, 0, 0x8000)
        k32.CloseHandle(hProcess)
        return False
    km = ctypes.windll.kernel32.GetModuleHandleW("kernel32.dll")
    ll = ctypes.windll.kernel32.GetProcAddress(km, b"LoadLibraryW")
    if not ll:
        print(f"  [!] GetProcAddress(LoadLibraryW) error: {ctypes.GetLastError()}")
        k32.VirtualFreeEx(hProcess, addr, 0, 0x8000)
        k32.CloseHandle(hProcess)
        return False
    tid = ctypes.c_uint32(0)
    thr = k32.CreateRemoteThread(hProcess, None, 0, ll, addr, 0, ctypes.byref(tid))
    if not thr:
        err = ctypes.GetLastError()
        print(f"  [!] CreateRemoteThread error: {err}")
        k32.VirtualFreeEx(hProcess, addr, 0, 0x8000)
        k32.CloseHandle(hProcess)
        return False
    k32.WaitForSingleObject(thr, 10000)
    k32.CloseHandle(thr)
    k32.CloseHandle(hProcess)
    return True

def main():
    print("=== MSWloader Launcher ===")
    dlls = ["planet_inject.dll", "MSWorld.dll"]
    for d in dlls:
        p = os.path.join(DLL_DIR, d)
        if os.path.exists(p):
            print(f"  [OK] {d} ({os.path.getsize(p)//1024}KB)")
        else:
            print(f"  [!] {d} NOT FOUND at {p}")
            return

    pid = find_maple()
    if not pid:
        print("\n[!] MapleStory not running")
        return
    print(f"\n[OK] MapleStory PID: {pid}")

    for d in dlls:
        p = os.path.join(DLL_DIR, d)
        print(f"Injecting {d}...")
        if inject_dll(pid, p):
            print(f"  [OK] {d}")
        else:
            print(f"  [!] {d} failed (err: {ctypes.GetLastError()})")
        time.sleep(0.5)

    print("\n[Done]")

if __name__ == "__main__":
    main()

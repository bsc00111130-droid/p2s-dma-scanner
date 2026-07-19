# -*- coding: utf-8 -*-
"""
MSWloader Full Reconstruction — DLL Injection + Hook + Lua Payload Framework
"""
import ctypes, os, sys, time, secrets, struct, json
from ctypes import wintypes

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
k32.OpenProcess.restype = wintypes.HANDLE
k32.VirtualAllocEx.argtypes = [wintypes.HANDLE, wintypes.LPVOID, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD]
k32.VirtualAllocEx.restype = wintypes.LPVOID
k32.VirtualFreeEx.argtypes = [wintypes.HANDLE, wintypes.LPVOID, ctypes.c_size_t, wintypes.DWORD]
k32.VirtualFreeEx.restype = wintypes.BOOL
k32.WriteProcessMemory.argtypes = [wintypes.HANDLE, wintypes.LPVOID, wintypes.LPCVOID, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
k32.WriteProcessMemory.restype = wintypes.BOOL
k32.ReadProcessMemory.argtypes = [wintypes.HANDLE, wintypes.LPCVOID, wintypes.LPVOID, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
k32.ReadProcessMemory.restype = wintypes.BOOL
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

PAGE_RWX = 0x40
MEM_COMMIT = 0x1000
MEM_RESERVE = 0x2000

BASE = os.path.dirname(os.path.abspath(__file__))

# ─── 1. Process Tools ─────────────────────────────────────────

def find_pid(name="msw.exe"):
    import subprocess
    try:
        out = subprocess.check_output(
            ["tasklist", "/FI", f"IMAGENAME eq {name}", "/FO", "CSV", "/NH"],
            creationflags=0x08000000, timeout=5).decode("cp949", errors="replace")
        for line in out.splitlines():
            if name.lower() in line.lower():
                pid = line.split(",")[1].strip().strip('"')
                if pid.isdigit(): return int(pid)
    except: pass
    return 0

def inject_dll(pid, dll_path):
    """Classic CreateRemoteThread + LoadLibrary injection"""
    h = k32.OpenProcess(0x1F0FFF, False, pid)
    if not h: return False, f"OpenProcess err={ctypes.get_last_error()}"
    buf = ctypes.create_unicode_buffer(os.path.abspath(dll_path))
    bs = ctypes.sizeof(buf)
    addr = k32.VirtualAllocEx(h, None, bs, MEM_COMMIT|MEM_RESERVE, 0x04)
    if not addr: k32.CloseHandle(h); return False, f"VirtualAllocEx err={ctypes.get_last_error()}"
    written = ctypes.c_size_t(0)
    if not k32.WriteProcessMemory(h, addr, buf, bs, ctypes.byref(written)):
        k32.VirtualFreeEx(h, addr, 0, 0x8000); k32.CloseHandle(h)
        return False, f"WriteProcessMemory err={ctypes.get_last_error()}"
    km = k32.GetModuleHandleW("kernel32.dll")
    ll = k32.GetProcAddress(km, b"LoadLibraryW")
    if not ll: k32.VirtualFreeEx(h, addr, 0, 0x8000); k32.CloseHandle(h); return False, "GetProcAddress failed"
    tid = wintypes.DWORD(0)
    thr = k32.CreateRemoteThread(h, None, 0, ll, addr, 0, ctypes.byref(tid))
    if not thr:
        err = ctypes.get_last_error(); k32.VirtualFreeEx(h, addr, 0, 0x8000); k32.CloseHandle(h)
        return False, f"CreateRemoteThread err={err}"
    k32.WaitForSingleObject(thr, 10000)
    k32.CloseHandle(thr); k32.CloseHandle(h)
    return True, "OK"

def inject_shellcode(pid, sc):
    """Raw shellcode injection via VirtualAllocEx + CreateRemoteThread"""
    h = k32.OpenProcess(0x1F0FFF, False, pid)
    if not h: return False, f"OpenProcess err={ctypes.get_last_error()}"
    addr = k32.VirtualAllocEx(h, None, len(sc), MEM_COMMIT|MEM_RESERVE, PAGE_RWX)
    if not addr: k32.CloseHandle(h); return False, f"VirtualAllocEx err={ctypes.get_last_error()}"
    written = ctypes.c_size_t(0)
    if not k32.WriteProcessMemory(h, addr, sc, len(sc), ctypes.byref(written)):
        k32.VirtualFreeEx(h, addr, 0, 0x8000); k32.CloseHandle(h)
        return False, f"WriteProcessMemory err={ctypes.get_last_error()}"
    tid = wintypes.DWORD(0)
    thr = k32.CreateRemoteThread(h, None, 0, addr, None, 0, ctypes.byref(tid))
    if not thr:
        err = ctypes.get_last_error(); k32.VirtualFreeEx(h, addr, 0, 0x8000); k32.CloseHandle(h)
        return False, f"CreateRemoteThread err={err}"
    k32.CloseHandle(thr); k32.CloseHandle(h)
    return True, f"tid={tid.value}"

# ─── 2. Memory Patch (NOP/jmp hooks) ──────────────────────────

def read_bytes(pid, addr, size):
    h = k32.OpenProcess(0x1F0FFF, False, pid)
    if not h: return None
    buf = (ctypes.c_uint8 * size)()
    read = ctypes.c_size_t(0)
    ok = k32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, size, ctypes.byref(read))
    k32.CloseHandle(h)
    return bytes(buf) if ok else None

def write_bytes(pid, addr, data):
    h = k32.OpenProcess(0x1F0FFF, False, pid)
    if not h: return False
    written = ctypes.c_size_t(0)
    ok = k32.WriteProcessMemory(h, ctypes.c_void_p(addr), bytes(data), len(data), ctypes.byref(written))
    k32.CloseHandle(h)
    return bool(ok)

def nop_range(pid, addr, length):
    return write_bytes(pid, addr, b'\x90' * length)

def detour_jmp(pid, src_addr, dst_addr):
    """Write a 14-byte jmp [rip+0] hook (x64)"""
    code = b'\xFF\x25\x00\x00\x00\x00' + struct.pack('<Q', dst_addr)
    return write_bytes(pid, src_addr, code)

# ─── 3. Module Resolution ──────────────────────────────────────

def get_module_base(pid, name):
    """Get module base address from process"""
    import subprocess
    try:
        out = subprocess.check_output(
            ["tasklist", "/M", name, "/FO", "CSV", "/NH"],
            creationflags=0x08000000, timeout=5).decode("cp949", errors="replace")
        # Parse PID and check if module is loaded
    except: pass
    return 0

# ─── 4. Lua Payload Injection ─────────────────────────────────

def prepare_lua_payload(lua_script, dest_path=None):
    """Write Lua payload to disk for planet_inject.dll to pick up"""
    if dest_path is None:
        temp = os.environ.get("TEMP", "C:\\Windows\\Temp")
        dest_path = os.path.join(temp, "MSW_Lua", "payload.lua")
    os.makedirs(os.path.dirname(dest_path), exist_ok=True)
    with open(dest_path, "w", encoding="utf-8") as f:
        f.write(lua_script)
    return dest_path

# ─── 5. Full Inject Sequence ───────────────────────────────────

def full_inject(pid, lua_script=None):
    """Inject all DLLs in correct order, then optionally push Lua payload"""
    results = {}
    order = ["net_hook.dll", "output_filter.dll", "MSWorld.dll"]
    for dll in order:
        path = os.path.join(BASE, dll)
        if not os.path.exists(path):
            results[dll] = "MISSING"
            continue
        ok, msg = inject_dll(pid, path)
        results[dll] = msg
        time.sleep(1.5)
    if lua_script:
        prepare_lua_payload(lua_script)
        results["lua"] = "staged"
    # Inject planet_inject.dll last (loaded by MSWorld)
    pip = os.path.join(BASE, "planet_inject.dll")
    if os.path.exists(pip):
        time.sleep(2)
        ok, msg = inject_dll(pid, pip)
        results["planet_inject.dll"] = msg
    return results

# ─── 6. Hook Injector DLL Source (C) ──────────────────────────

HOOK_DLL_SOURCE = r'''
#include <windows.h>
#include <detours.h>
#pragma comment(lib, "detours.lib")

// Example: hook send() to capture network packets
int (WINAPI *Real_send)(SOCKET s, const char* buf, int len, int flags) = send;
int WINAPI Hook_send(SOCKET s, const char* buf, int len, int flags) {
    // Log packet
    FILE* f = fopen("C:\\msw\\packet_log.txt", "ab");
    if (f) {
        DWORD pid = GetCurrentProcessId();
        fwrite(&pid, 4, 1, f);
        fwrite(&len, 4, 1, f);
        fwrite(buf, 1, len, f);
        fclose(f);
    }
    return Real_send(s, buf, len, flags);
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(reason);
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach(&(PVOID&)Real_send, Hook_send);
        DetourTransactionCommit();
    }
    return TRUE;
}
'''

# ─── 7. CLI ───────────────────────────────────────────────────

if __name__ == "__main__":
    import argparse
    p = argparse.ArgumentParser(description="MSWloader Reconstruction")
    p.add_argument("action", nargs="?", default="inject",
        choices=["inject", "read", "write", "nop", "detour", "lua"])
    p.add_argument("--pid", type=int, default=0)
    p.add_argument("--addr", type=lambda x: int(x,16), default=0)
    p.add_argument("--data", type=lambda x: bytes.fromhex(x), default=b"")
    p.add_argument("--size", type=int, default=0)
    p.add_argument("--dll", default="")
    p.add_argument("--lua", default="")
    args = p.parse_args()

    if not args.pid:
        args.pid = find_pid()
    if not args.pid:
        print("[!] msw.exe not running")
        sys.exit(1)

    if args.action == "inject":
        if args.dll:
            ok, msg = inject_dll(args.pid, args.dll)
            print(f"[{'OK' if ok else 'FAIL'}] {msg}")
        else:
            print(f"[*] Full inject into PID {args.pid}")
            if args.lua:
                with open(args.lua) as f: lua = f.read()
            else: lua = None
            results = full_inject(args.pid, lua)
            for k, v in results.items():
                print(f"  {k}: {v}")

    elif args.action == "read" and args.addr and args.size:
        data = read_bytes(args.pid, args.addr, args.size)
        if data:
            for i in range(0, len(data), 16):
                h = " ".join(f"{b:02x}" for b in data[i:i+16])
                a = "".join(chr(b) if 32<=b<127 else "." for b in data[i:i+16])
                print(f"{i:04x}: {h:<48s} {a}")
        else:
            print("[!] Read failed")

    elif args.action == "write" and args.addr and args.data:
        ok = write_bytes(args.pid, args.addr, args.data)
        print(f"[{'OK' if ok else 'FAIL'}] Write {len(args.data)} bytes")

    elif args.action == "nop" and args.addr and args.size:
        ok = nop_range(args.pid, args.addr, args.size)
        print(f"[{'OK' if ok else 'FAIL'}] NOP {args.size} bytes")

    elif args.action == "lua" and args.lua:
        path = prepare_lua_payload(args.lua)
        print(f"[OK] Lua staged at {path}")

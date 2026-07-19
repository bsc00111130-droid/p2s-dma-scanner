"""
dma_physmem.py  —  Python DMA Physical Memory Reader
Backend 1: Kernel driver (IOCTL_DMA_READ) — directly maps FPGA BAR
Backend 2: MemProcFS named pipe — fallback
Backend 3: Raw physical via /dev/cma or /proc/... (Linux, future)
"""
import ctypes
import ctypes.wintypes
import struct
from typing import Optional

IOCTL_DMA_READ  = 0x82200803  # CTL_CODE(0x22, 0x820, METHOD_BUFFERED, 3)

class DmaPhysMemRequest(ctypes.Structure):
    _fields_ = [
        ("Version",     ctypes.c_uint32),
        ("Size",        ctypes.c_uint32),
        ("ProcessId",   ctypes.c_uint32),
        ("Pad0",        ctypes.c_uint32),
        ("VirtualAddr", ctypes.c_uint64),
        ("ReadSize",    ctypes.c_uint32),
        ("Flags",       ctypes.c_uint32),
        ("Result",      ctypes.c_uint64),
    ]

class DmaPhysMemResponse(ctypes.Structure):
    _fields_ = [
        ("Version",          ctypes.c_uint32),
        ("Size",             ctypes.c_uint32),
        ("ProcessId",        ctypes.c_uint32),
        ("VirtualAddrHi",    ctypes.c_uint32),
        ("VirtualAddr",      ctypes.c_uint64),
        ("RequestedSize",    ctypes.c_uint32),
        ("Flags",            ctypes.c_uint32),
        ("PhysAddr",         ctypes.c_uint64),
        ("BytesTransferred", ctypes.c_uint32),
        ("TranslationStatus", ctypes.c_int32),
        ("DmaStatus",        ctypes.c_int32),
        ("Data",             ctypes.c_uint8 * 4096),
    ]


class PhysMemReader:
    def __init__(self):
        self._handle = None
        self._pipe = None

    # === Backend 1: Kernel Driver IOCTL ===
    def open_driver(self, devpath: str = r"\\.\DmaPhysMem") -> bool:
        self._handle = ctypes.windll.kernel32.CreateFileW(
            devpath, 0xC0000000, 0, None, 3, 0, None)
        if not self._handle or self._handle == -1:
            self._handle = None
            return False
        return True

    def close(self):
        if self._handle:
            ctypes.windll.kernel32.CloseHandle(self._handle)
            self._handle = None

    def read_virtual(self, pid: int, va: int, size: int) -> Optional[bytes]:
        if not self._handle:
            return self._read_virtual_fallback(pid, va, size)

        req = DmaPhysMemRequest()
        req.Version = 0x00010001
        req.Size = ctypes.sizeof(req)
        req.ProcessId = pid
        req.VirtualAddr = va
        req.ReadSize = min(size, 4096)
        req.Flags = 0

        resp = DmaPhysMemResponse()
        ret = ctypes.c_uint32(0)

        ok = ctypes.windll.kernel32.DeviceIoControl(
            self._handle, IOCTL_DMA_READ,
            ctypes.byref(req), ctypes.sizeof(req),
            ctypes.byref(resp), ctypes.sizeof(resp),
            ctypes.byref(ret), None)
        if not ok:
            return self._read_virtual_fallback(pid, va, size)

        if resp.TranslationStatus != 0 or resp.DmaStatus != 0:
            return None

        return bytes(resp.Data[:resp.BytesTransferred])

    def read_physical(self, pa: int, size: int) -> Optional[bytes]:
        """Direct physical read via FPGA, PID=0 means use physical addr directly"""
        return self.read_virtual(0, pa, size)

    # === Backend 2: MemProcFS pipe fallback ===
    def _read_virtual_fallback(self, pid: int, va: int, size: int) -> Optional[bytes]:
        try:
            if not self._pipe:
                self._pipe = self._connect_pipe()
            return self._pipe_read(pid, va, size)
        except:
            return None

    def _connect_pipe(self):
        import win32pipe, win32file
        h = win32file.CreateFile(
            r"\\.\pipe\memprocfs",
            win32file.GENERIC_READ | win32file.GENERIC_WRITE,
            0, None, win32file.OPEN_EXISTING, 0, None)
        return h

    def _pipe_read(self, pid: int, va: int, size: int) -> Optional[bytes]:
        import win32file, struct
        cmd = struct.pack("<II", 3, 12) + struct.pack("<QI", va, size)
        win32file.WriteFile(self._pipe, cmd)
        _, hdr = win32file.ReadFile(self._pipe, 8)
        _, resp_size = struct.unpack("<II", hdr)
        if resp_size != size:
            return None
        _, data = win32file.ReadFile(self._pipe, resp_size)
        return data


if __name__ == "__main__":
    import sys

    r = PhysMemReader()
    ok = r.open_driver()
    print(f"[+] Kernel driver: {'loaded' if ok else 'NOT loaded (fallback)'}")

    if len(sys.argv) >= 3:
        pid = int(sys.argv[1])
        addr = int(sys.argv[2], 16)
        size = int(sys.argv[3]) if len(sys.argv) > 3 else 4
        data = r.read_virtual(pid, addr, size)
        if data:
            print(f"Read {len(data)} bytes from PID {pid} @ {hex(addr)}:")
            print(" ".join(f"{b:02x}" for b in data))
        else:
            print("[!] Read failed")
    else:
        print("Usage: python dma_physmem.py <pid> <hex_addr> [size]")

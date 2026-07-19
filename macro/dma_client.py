import struct
import win32pipe
import win32file
import pywintypes
import logging
from typing import Optional

logger = logging.getLogger("dma")

class MemProcFSClient:
    def __init__(self, pipe_name: str = r"\\.\pipe\memprocfs"):
        self.pipe_name = pipe_name
        self.handle = None

    def connect(self) -> bool:
        try:
            self.handle = win32file.CreateFile(
                self.pipe_name,
                win32file.GENERIC_READ | win32file.GENERIC_WRITE,
                0, None, win32file.OPEN_EXISTING, 0, None
            )
            win32pipe.SetNamedPipeHandleState(self.handle, 1, None, None)
            logger.info("Connected to MemProcFS pipe")
            return True
        except pywintypes.error as e:
            logger.error(f"Failed to connect to MemProcFS: {e}")
            return False

    def close(self):
        if self.handle:
            win32file.CloseHandle(self.handle)
            self.handle = None

    def _send_cmd(self, cmd: int, data: bytes = b"") -> bytes:
        if not self.handle:
            raise ConnectionError("Not connected")
        header = struct.pack("<II", cmd, len(data))
        win32file.WriteFile(self.handle, header + data)
        resp_header = win32file.ReadFile(self.handle, 8)
        if resp_header:
            _, resp_len = struct.unpack("<II", resp_header[1])
            resp_data = win32file.ReadFile(self.handle, resp_len) if resp_len else (None, b"")
            return resp_data[1] if resp_len else b""
        return b""

    def read_phys(self, addr: int, size: int) -> Optional[bytes]:
        data = self._send_cmd(3, struct.pack("<QI", addr, size))
        return data if len(data) == size else None

    def write_phys(self, addr: int, data: bytes) -> bool:
        resp = self._send_cmd(4, struct.pack("<Q", addr) + data)
        return True

    def read_i64(self, addr: int) -> Optional[int]:
        d = self.read_phys(addr, 8)
        return struct.unpack("<q", d)[0] if d else None

    def read_i32(self, addr: int) -> Optional[int]:
        d = self.read_phys(addr, 4)
        return struct.unpack("<i", d)[0] if d else None

    def read_ptr(self, addr: int) -> Optional[int]:
        return self.read_i64(addr)

    def read_bytes(self, addr: int, size: int) -> Optional[bytes]:
        return self.read_phys(addr, size)

    def read_str(self, addr: int, max_len: int = 64) -> Optional[str]:
        d = self.read_phys(addr, max_len)
        if d:
            null = d.find(b"\x00")
            return d[:null].decode("utf-16-le", errors="replace") if null >= 0 else d.decode("utf-16-le", errors="replace")
        return None
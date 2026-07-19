"""
proc_ioctl_readmem.py  —  Python test for IOCTL_PROC_IOCTL_READ_PROCESS_MEMORY
Usage: python proc_ioctl_readmem.py <pid> <hex_addr> [size]
"""
import ctypes
import ctypes.wintypes
import sys

IOCTL_READ_PROCESS_MEMORY = 0x82201E07

class ReadMemRequest(ctypes.Structure):
    _fields_ = [
        ("ProcessId", ctypes.c_uint32),
        ("Pad",       ctypes.c_uint32),
        ("Address",   ctypes.c_uint64),
        ("Size",      ctypes.c_size_t),
    ]

class ReadMemResponse(ctypes.Structure):
    _fields_ = [
        ("ProcessId",          ctypes.c_uint32),
        ("Pad0",               ctypes.c_uint32),
        ("Address",            ctypes.c_uint64),
        ("RequestedSize",      ctypes.c_size_t),
        ("ProcessFound",       ctypes.c_uint32),
        ("SizeAccepted",       ctypes.c_uint32),
        ("AddressRangeValid",  ctypes.c_uint32),
        ("ValidationStatus",   ctypes.c_int32),
        ("PayloadSize",        ctypes.c_size_t),
        ("CopyStatus",         ctypes.c_int32),
        ("Buffer",             ctypes.c_uint8 * 0x1000),
    ]

def read_process_memory(pid: int, va: int, size: int) -> bytes:
    hDev = ctypes.windll.kernel32.CreateFileW(
        r"\\.\ProcIoctlDemo",
        0xC0000000, 0, None, 3, 0, None)
    if not hDev or hDev == -1:
        raise RuntimeError(f"Open driver failed: {ctypes.GetLastError()}")

    req = ReadMemRequest()
    req.ProcessId = pid
    req.Address = va
    req.Size = min(size, 0x1000)

    out_size = ctypes.sizeof(ReadMemResponse) - 0x1000 + req.Size
    out_buf = (ctypes.c_uint8 * out_size)()
    ctypes.memmove(out_buf, ctypes.byref(req), ctypes.sizeof(req))

    ret = ctypes.c_uint32(0)
    ok = ctypes.windll.kernel32.DeviceIoControl(
        hDev, IOCTL_READ_PROCESS_MEMORY,
        out_buf, ctypes.sizeof(req),
        out_buf, out_size,
        ctypes.byref(ret), None)
    ctypes.windll.kernel32.CloseHandle(hDev)

    if not ok:
        raise RuntimeError(f"IOCTL failed: {ctypes.GetLastError()}")

    resp = ReadMemResponse.from_buffer_copy(bytes(out_buf))
    if resp.ValidationStatus != 0:
        raise RuntimeError(f"Validation failed: NTSTATUS=0x{resp.ValidationStatus:08x}")
    if resp.CopyStatus != 0:
        raise RuntimeError(f"Copy failed: NTSTATUS=0x{resp.CopyStatus:08x}")

    return bytes(out_buf[ctypes.sizeof(ReadMemResponse) - 0x1000:])[:resp.PayloadSize]


if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <pid> <hex_addr> [size]")
        sys.exit(1)

    pid = int(sys.argv[1])
    addr = int(sys.argv[2], 16)
    size = int(sys.argv[3]) if len(sys.argv) > 3 else 64

    try:
        data = read_process_memory(pid, addr, size)
        print(f"Read {len(data)} bytes from PID {pid} @ {hex(addr)}:")
        for i in range(0, len(data), 16):
            hex_str = " ".join(f"{b:02x}" for b in data[i:i+16])
            asc_str = "".join(chr(b) if 32 <= b < 127 else "." for b in data[i:i+16])
            print(f"  {i:04x}: {hex_str:<48s}  {asc_str}")
    except Exception as e:
        print(f"[!] {e}")
        sys.exit(1)

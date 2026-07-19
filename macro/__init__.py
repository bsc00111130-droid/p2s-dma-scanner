"""
dma_reader — C++ 고속 DMA 메모리 리더 Python 바인딩
Fallback: C++ DLL 없으면 순수 Python 모드로 동작
"""
import ctypes
import os
import struct
from ctypes import wintypes

_dll = None
_loaded = False

def load_dll(path: str = None) -> bool:
    global _dll, _loaded
    if _loaded:
        return _dll is not None
    if path is None:
        path = os.path.join(os.path.dirname(__file__), "dma_reader", "x64", "Release", "dmareader.dll")
    if os.path.exists(path):
        try:
            _dll = ctypes.WinDLL(path)
            _dll.PtrResolve.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint64]
            _dll.PtrResolve.restype = ctypes.c_uint64
            _dll.ModuleFind.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p]
            _dll.ModuleFind.restype = ctypes.c_uint64
            _loaded = True
            return True
        except:
            pass
    _loaded = True
    return False

def is_dll_loaded() -> bool:
    return _dll is not None

class FieldType:
    I8, U8, I16, U16, I32, U32, I64, U64, F32, F64, BYTES, STR16, PTR64 = range(13)

TYPE_SIZES = {FieldType.I8:1, FieldType.U8:1, FieldType.I16:2, FieldType.U16:2,
              FieldType.I32:4, FieldType.U32:4, FieldType.I64:8, FieldType.U64:8,
              FieldType.F32:4, FieldType.F64:8, FieldType.PTR64:8}

class FieldDesc:
    __slots__ = ('name', 'offset', 'size', 'type')
    def __init__(self, name: str, offset: int, ftype: int):
        self.name = name
        self.offset = offset
        self.size = TYPE_SIZES.get(ftype, 4)
        self.type = ftype

class Snapshot:
    __slots__ = ('seq', 'tsc', 'us', 'resolve_cycles', 'values')
    def __init__(self):
        self.seq = 0
        self.tsc = 0
        self.us = 0
        self.resolve_cycles = 0
        self.values = {}

def parse_binary(data: bytes, fields: list, base_offset: int = 0) -> dict:
    result = {}
    for f in fields:
        off = base_offset + f.offset
        if off + f.size > len(data):
            result[f.name] = None
            continue
        chunk = data[off:off + f.size]
        try:
            if f.type == FieldType.I32:
                result[f.name] = struct.unpack('<i', chunk)[0]
            elif f.type == FieldType.U32:
                result[f.name] = struct.unpack('<I', chunk)[0]
            elif f.type == FieldType.I64:
                result[f.name] = struct.unpack('<q', chunk)[0]
            elif f.type == FieldType.U64:
                result[f.name] = struct.unpack('<Q', chunk)[0]
            elif f.type == FieldType.F32:
                result[f.name] = struct.unpack('<f', chunk)[0]
            elif f.type == FieldType.F64:
                result[f.name] = struct.unpack('<d', chunk)[0]
            elif f.type == FieldType.STR16:
                null = chunk.find(b'\x00\x00')
                result[f.name] = chunk[:null].decode('utf-16-le', errors='replace') if null >= 0 else ''
            elif f.type == FieldType.PTR64:
                result[f.name] = struct.unpack('<Q', chunk)[0]
        except:
            result[f.name] = None
    return result

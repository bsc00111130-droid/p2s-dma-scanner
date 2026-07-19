import json
import time
import os
import sys
from threading import Thread, Lock
from typing import Optional
from dma_reader import FieldDesc, FieldType, parse_binary, load_dll, is_dll_loaded

# DMA backend: try kernel driver first, fall back to MemProcFS pipe
sys.path.insert(0, r"C:\Users\zAmA\Desktop\codex\idea")
try:
    from dma_physmem import PhysMemReader as DmaBackend
    _backend = DmaBackend()
    _have_driver = _backend.open_driver()
    if not _have_driver:
        from dma_client import MemProcFSClient as DmaBackend
        _backend = DmaBackend(r"\\.\pipe\memprocfs")
except:
    from dma_client import MemProcFSClient as DmaBackend
    _backend = DmaBackend(r"\\.\pipe\memprocfs")
    _have_driver = False

CONFIG_PATH = os.path.join(os.path.dirname(__file__), "config.json")
CACHELINE = 64

DEFAULT_CONFIG = {
    "pipe_name": r"\\.\pipe\memprocfs",
    "interval_us": 1000,
    "use_cpp": True,
    "module_name": "MapleStory.exe",
    "ptr_chain": [0x1A0, 0x2B8, 0x100, 0x50],
    "read_size": 64,
    "fields": [
        {"name": "x",       "offset": 0,  "type": "f32"},
        {"name": "y",       "offset": 4,  "type": "f32"},
        {"name": "hp",      "offset": 8,  "type": "i32"},
        {"name": "max_hp",  "offset": 12, "type": "i32"},
        {"name": "mp",      "offset": 16, "type": "i32"},
        {"name": "max_mp",  "offset": 20, "type": "i32"},
        {"name": "level",   "offset": 24, "type": "i32"},
        {"name": "map_id",  "offset": 28, "type": "i32"}
    ],
    "mobs": {
        "enabled": True,
        "ptr_chain": [0x310, 0x2B8, 0x50],
        "read_size": 64,
        "count_offset": 0,
        "max_count": 50,
        "stride": 256,
        "fields": [
            {"name": "x",     "offset": 0,  "type": "f32"},
            {"name": "y",     "offset": 4,  "type": "f32"},
            {"name": "hp",    "offset": 8,  "type": "i32"},
            {"name": "max_hp","offset": 12, "type": "i32"},
            {"name": "id",    "offset": 16, "type": "i32"}
        ]
    }
}

TYPE_MAP = {
    "i32": FieldType.I32, "u32": FieldType.U32,
    "i64": FieldType.I64, "u64": FieldType.U64,
    "f32": FieldType.F32, "f64": FieldType.F64,
    "str16": FieldType.STR16, "ptr": FieldType.PTR64,
}

def load_config():
    if os.path.exists(CONFIG_PATH):
        with open(CONFIG_PATH) as f:
            return json.load(f)
    return dict(DEFAULT_CONFIG)

def save_config(cfg: dict):
    with open(CONFIG_PATH, "w") as f:
        json.dump(cfg, f, indent=2)

def resolve_chain(dma, pid: int, chain: list, base: int, driver_mode: bool = False) -> Optional[int]:
    addr = base
    for off in chain:
        if driver_mode:
            data = dma.read_virtual(pid, addr + off, 8)
            if not data:
                return None
            v = int.from_bytes(data, 'little')
        else:
            v = dma.read_ptr(addr + off)
            if v is None:
                return None
        addr = v
    return addr

class Reader:
    def __init__(self, cfg: dict):
        self.cfg = cfg
        self.dma = _backend
        self.driver_mode = _have_driver
        self._running = False
        self._data = {}
        self._lock = Lock()
        self._callbacks = []
        self._seq = 0
        self._last_stats = None

        load_dll()
        self._fields = []
        for fd in cfg.get("fields", []):
            ft = TYPE_MAP.get(fd.get("type", "i32"), FieldType.I32)
            self._fields.append(FieldDesc(fd["name"], fd["offset"], ft))

    def on_update(self, cb):
        self._callbacks.append(cb)

    def get(self) -> dict:
        with self._lock:
            return dict(self._data)

    def _loop(self):
        while self._running:
            t_start = time.perf_counter_ns() // 1000

            try:
                # Get PID once
                if not hasattr(self, '_pid'):
                    self._pid = self._get_target_pid()
                    if not self._pid:
                        time.sleep(1)
                        continue
                    if self.cfg.get("module_name"):
                        mod_base = self._get_module_base(self._pid, self.cfg["module_name"])
                        if mod_base:
                            self.cfg["_mod_base"] = mod_base

                pid = self._pid
                base = self.cfg.get("_mod_base", 0x400000)

                # Step 1: resolve pointer chain
                chain = self.cfg.get("ptr_chain", [])

                t0 = time.perf_counter_ns()
                target = resolve_chain(self.dma, pid, chain, base, self.driver_mode)
                t1 = time.perf_counter_ns()
                resolve_us = (t1 - t0) // 1000

                if target is None:
                    time.sleep(0.001)
                    continue

                # Step 2: DMA bulk read
                read_size = self.cfg.get("read_size", 64)
                read_size = ((read_size + CACHELINE - 1) // CACHELINE) * CACHELINE

                if self.driver_mode:
                    raw = self.dma.read_virtual(pid, target, read_size)
                else:
                    raw = self.dma.read_bytes(target, read_size)
                t2 = time.perf_counter_ns()

                if raw is None:
                    continue

                # Step 3: parse
                values = parse_binary(raw, self._fields)
                self._seq += 1
                values["_seq"] = self._seq
                values["_resolve_us"] = resolve_us
                values["_read_us"] = (t2 - t1) // 1000
                values["_total_us"] = (t2 - t0) // 1000

                # mobs
                if self.cfg.get("mobs", {}).get("enabled", False):
                    mobs_cfg = self.cfg["mobs"]
                    m_chain = mobs_cfg.get("ptr_chain", [])
                    m_target = resolve_chain(self.dma, pid, m_chain, base, self.driver_mode)
                    if m_target:
                        count_addr = m_target + mobs_cfg.get("count_offset", 0)
                        if self.driver_mode:
                            cd = self.dma.read_virtual(pid, count_addr, 4)
                            count = int.from_bytes(cd, 'little') if cd else 0
                        else:
                            count = self.dma.read_i32(count_addr) or 0
                        mobs = []
                        if count > 0:
                            count = min(count, mobs_cfg.get("max_count", 50))
                            stride = mobs_cfg.get("stride", 256)
                            m_fields = []
                            for fd in mobs_cfg.get("fields", []):
                                ft = TYPE_MAP.get(fd.get("type", "i32"), FieldType.I32)
                                m_fields.append(FieldDesc(fd["name"], fd["offset"], ft))
                            for i in range(count):
                                item_addr = m_target + i * stride
                                if self.driver_mode:
                                    mob_data = self.dma.read_virtual(pid, item_addr, stride)
                                else:
                                    mob_data = self.dma.read_bytes(item_addr, stride)
                                if mob_data:
                                    mobs.append(parse_binary(mob_data, m_fields))
                        values["_mobs"] = mobs

                with self._lock:
                    self._data = values

                for cb in self._callbacks:
                    cb(values)

            except Exception as e:
                pass

            elapsed_us = (time.perf_counter_ns() // 1000) - t_start
            interval = self.cfg.get("interval_us", 1000)
            sleep_us = max(1, interval - elapsed_us)
            time.sleep(sleep_us / 1_000_000)

    def _get_target_pid(self) -> Optional[int]:
        import psutil
        name = self.cfg.get("target_process", "MapleStory.exe")
        for p in psutil.process_iter(['pid', 'name']):
            if p.info['name'] and p.info['name'].lower() == name.lower():
                return p.info['pid']
        return None

    def _get_module_base(self, pid: int, module_name: str) -> Optional[int]:
        import psutil
        try:
            p = psutil.Process(pid)
            for mmap in p.memory_maps():
                if module_name.lower() in mmap.path.lower():
                    return mmap.addr
        except:
            pass
        return None

    def start(self):
        if not self.driver_mode:
            if not self.dma.connect():
                return False
        self._running = True
        Thread(target=self._loop, daemon=True).start()
        return True

    def stop(self):
        self._running = False
        self.dma.close()

def display(data: dict):
    os.system("cls" if os.name == "nt" else "clear")
    print("DMA Reader — Real-time Memory Monitor")
    print("=" * 60)
    print(f"Seq: {data.get('_seq', 0)}")
    print(f"Resolve: {data.get('_resolve_us', 0)}μs | Read: {data.get('_read_us', 0)}μs | Total: {data.get('_total_us', 0)}μs")
    print()

    print("Player:")
    cpp = is_dll_loaded()
    print(f"  X={data.get('x')}  Y={data.get('y')}")
    print(f"  HP={data.get('hp')}/{data.get('max_hp')}")
    print(f"  MP={data.get('mp')}/{data.get('max_mp')}")
    print(f"  Level={data.get('level')}")
    print(f"  MapID={data.get('map_id')}")
    print()

    mobs = data.get('_mobs', [])
    alive = [m for m in mobs if m.get('hp', 0) > 0]
    print(f"Mobs: {len(alive)} nearby")
    for m in alive[:5]:
        print(f"  ID={m.get('id')} HP={m.get('hp')}/{m.get('max_hp')} @({m.get('x')},{m.get('y')})")
    print()
    cpp = is_dll_loaded()
    dm = "driver" if _have_driver else "pipe"
    print(f"[Backend: {dm} | C++: {'loaded' if cpp else 'python'}]")
    print("Ctrl+C to stop")

if __name__ == "__main__":
    cfg = load_config()
    r = Reader(cfg)
    r.on_update(display)
    if r.start():
        try:
            while True:
                time.sleep(0.1)
        except KeyboardInterrupt:
            r.stop()
    else:
        print("Failed to connect. Is MemProcFS running?")
        sys.exit(1)

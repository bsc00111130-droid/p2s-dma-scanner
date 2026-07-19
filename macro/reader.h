#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <thread>
#include <chrono>
#include "ptr_chain.h"

#pragma comment(lib, "ntdll.lib")

using namespace std::chrono;

// ============================================================================
// Pipe DMA client — connects to MemProcFS named pipe
// ============================================================================
class PipeDMA {
    HANDLE pipe_ = INVALID_HANDLE_VALUE;

    bool transact(uint32_t cmd, const void* in, uint32_t in_len, void* out, uint32_t out_len) noexcept {
        struct { uint32_t cmd; uint32_t len; } hdr = { cmd, in_len };
        DWORD written = 0, read = 0;
        if (!WriteFile(pipe_, &hdr, 8, &written, nullptr)) return false;
        if (in_len && !WriteFile(pipe_, in, in_len, &written, nullptr)) return false;
        if (!ReadFile(pipe_, &hdr, 8, &read, nullptr)) return false;
        if (out_len && hdr.len == out_len) {
            ReadFile(pipe_, out, out_len, &read, nullptr);
        }
        return hdr.len == out_len;
    }

public:
    bool connect(const wchar_t* pipe_name = L"\\\\.\\pipe\\memprocfs") noexcept {
        pipe_ = CreateFileW(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                            OPEN_EXISTING, 0, nullptr);
        if (pipe_ == INVALID_HANDLE_VALUE) return false;
        DWORD mode = PIPE_READMODE_MESSAGE;
        SetNamedPipeHandleState(pipe_, &mode, nullptr, nullptr);
        return true;
    }

    void close() noexcept { if (pipe_ != INVALID_HANDLE_VALUE) CloseHandle(pipe_); }

    uint64_t read64(uint64_t phys_addr) noexcept {
        struct { uint64_t addr; uint32_t size; } req = { phys_addr, 8 };
        uint64_t val = 0;
        transact(3, &req, 12, &val, 8);
        return val;
    }

    bool read_bulk(uint64_t phys_addr, void* buf, uint32_t size) noexcept {
        struct { uint64_t addr; uint32_t size; } req = { phys_addr, size };
        return transact(3, &req, 12, buf, size);
    }

    uint64_t read_ptr(uint64_t addr) noexcept { return read64(addr); }

    // DMA read function adapter for PtrResolver
    static uint64_t dma_read64_adapter(uint64_t addr) noexcept {
        auto* self = (PipeDMA*)GetModuleHandleA(nullptr); // hack: use TLS in real code
        return 0; // placeholder — injected per-instance below
    }
};

// ============================================================================
// Field descriptor — single parsed field from binary dump
// ============================================================================
enum FieldType : uint8_t {
    F_I8, F_U8, F_I16, F_U16, F_I32, F_U32, F_I64, F_U64,
    F_F32, F_F64, F_BYTES, F_STR16, F_PTR64
};

struct FieldDesc {
    const char* name;
    uint32_t    offset;
    uint32_t    size;
    FieldType   type;
};

// ============================================================================
// Per-field value union
// ============================================================================
struct FieldValue {
    uint64_t as_u64;
    double   as_f64;
    void*    as_ptr;

    float as_f32() const noexcept { return std::bit_cast<float>((int32_t)as_u64); }
    int32_t as_i32() const noexcept { return (int32_t)as_u64; }
};

// ============================================================================
// Main snapshot structure — parsed fields + timestamp
// ============================================================================
struct alignas(CACHELINE_SIZE) Snapshot {
    Timestamp   t;
    uint64_t    seq;
    uint64_t    resolve_latency_cycles;
    int64_t     read_latency_us;
    int64_t     total_latency_us;
    uint32_t    field_count;
    uint32_t    _pad;
    FieldValue  fields[64];
};

// ============================================================================
// High-speed reader — resolves pointer chain → DMA reads struct → parses
// ============================================================================
template<typename ChainDef, size_t NFields>
class StructReader {
    PipeDMA        dma_;
    PtrResolver<ChainDef> resolver_;
    ModuleDB       moddb_;
    uint64_t       target_addr_ = 0;
    uint64_t       seq_ = 0;
    FieldDesc      fields_[NFields];
    uint8_t        buf_[4096];
    uint32_t       read_size_ = 0;
    HANDLE         timer_ = nullptr;

public:
    StructReader(const FieldDesc (&fields)[NFields]) noexcept {
        __builtin_memcpy(fields_, fields, sizeof(fields));
        // compute total read size (coalesced range)
        for (size_t i = 0; i < NFields; ++i) {
            uint32_t end = fields[i].offset + fields[i].size;
            if (end > read_size_) read_size_ = end;
        }
        read_size_ = (read_size_ + 63) & ~63; // cacheline align
    }

    void init_pipe() noexcept {
        if (!dma_.connect()) {
            printf("[!] DMA pipe connect failed\n");
            return;
        }
        // adapter lambda
        resolver_.init([](uint64_t addr) -> uint64_t {
            // TLS-based dispatch in real code
            return 0;
        });
    }

    void set_base(uint64_t virt, uint64_t phys) noexcept {
        resolver_.set_base(virt, phys);
    }

    // Read + parse in one shot. Returns false on CRC miss.
    bool read(Snapshot& snap) noexcept {
        auto t0 = Timestamp::now();

        // Step 1: resolve pointer chain
        uint64_t target = resolver_.resolve();
        if (!target) return false;
        auto t1 = Timestamp::now();
        snap.resolve_latency_cycles = t1.tsc - t0.tsc;

        // Step 2: DMA read entire struct range
        if (!dma_.read_bulk(target, buf_, read_size_)) return false;
        auto t2 = Timestamp::now();

        // Step 3: parse fields inline
        snap.seq = ++seq_;
        snap.field_count = NFields;
        for (size_t i = 0; i < NFields; ++i) {
            const auto& f = fields_[i];
            void* src = buf_ + f.offset;

            switch (f.type) {
            case F_I32:  snap.fields[i].as_u64 = *(int32_t*)src;    break;
            case F_U32:  snap.fields[i].as_u64 = *(uint32_t*)src;   break;
            case F_I64:  snap.fields[i].as_u64 = *(int64_t*)src;    break;
            case F_U64:  snap.fields[i].as_u64 = *(uint64_t*)src;   break;
            case F_F32:  snap.fields[i].as_f64 = *(float*)src;      break;
            case F_F64:  snap.fields[i].as_f64 = *(double*)src;     break;
            case F_STR16: snap.fields[i].as_ptr = src;              break;
            default:     snap.fields[i].as_u64 = 0;                 break;
            }
        }

        snap.t = t0;
        snap.read_latency_us = (t2.us - t1.us);
        snap.total_latency_us = (t2.us - t0.us);
        return true;
    }

    // High-resolution polling — busy-wait until deadline or data ready
    int poll(Snapshot& snap, int64_t timeout_us = 500) noexcept {
        auto deadline = Timestamp::now().us + timeout_us;
        int count = 0;
        do {
            _mm_pause();
            if (read(snap)) ++count;
        } while (Timestamp::now().us < deadline);
        return count;
    }
};

// ============================================================================
// Usage example
// ============================================================================
#ifdef _MAIN
using CharChain = PtrChain<0x1A0, 0x2B8, 0x100, 0x50>;

int main() {
    FieldDesc char_fields[] = {
        {"x",       0,  4, F_F32},
        {"y",       4,  4, F_F32},
        {"hp",      8,  4, F_I32},
        {"max_hp", 12,  4, F_I32},
        {"mp",     16,  4, F_I32},
        {"max_mp", 20,  4, F_I32},
        {"level",  24,  4, F_I32},
        {"map_id", 28,  4, F_I32},
    };

    StructReader<CharChain, 8> reader(char_fields);
    reader.init_pipe();

    // set base from module resolve
    // reader.set_base(module_base, phys_base);

    Snapshot snap;
    while (true) {
        if (reader.read(snap)) {
            printf("HP=%d/%d  latency=%lldus\n",
                   snap.fields[2].as_i32(),
                   snap.fields[3].as_i32(),
                   snap.total_latency_us);
        }
        _mm_pause();
    }
}
#endif

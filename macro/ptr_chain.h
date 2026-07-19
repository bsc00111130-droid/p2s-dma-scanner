#pragma once
#include <cstdint>
#include <cstring>
#include <atomic>
#include <bit>
#include <immintrin.h>
#include <mmintrin.h>
#include <xmmintrin.h>

#define CACHELINE_SIZE 64
#define ALIGN_CACHE alignas(CACHELINE_SIZE)
#define ALIGN_PAGE 4096

struct alignas(16) Timestamp {
    uint64_t tsc;
    uint64_t us;

    static Timestamp now() noexcept {
        Timestamp t;
        t.tsc = __rdtsc();
        LARGE_INTEGER q;
        QueryPerformanceCounter(&q);
        static double freq = []{ LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (double)f.QuadPart; }();
        t.us = (uint64_t)((double)q.QuadPart * 1000000.0 / freq);
        return t;
    }
};

template<typename T, uint32_t CAP>
requires ((CAP & (CAP - 1)) == 0)
class SPSCQueue {
    static constexpr uint32_t MASK = CAP - 1;
    T slots_[CAP] ALIGN_CACHE;
    ALIGN_CACHE std::atomic<uint32_t> head_{0};
    ALIGN_CACHE std::atomic<uint32_t> tail_{0};
public:
    bool push(const T& item) noexcept {
        uint32_t h = head_.load(std::memory_order_relaxed);
        if ((h - tail_.load(std::memory_order_acquire)) == CAP) return false;
        slots_[h & MASK] = item;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }
    bool pop(T& out) noexcept {
        uint32_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) return false;
        out = slots_[t & MASK];
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }
    uint32_t count() const noexcept {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }
};

template<uint64_t... Offsets>
struct PtrChain {
    static constexpr size_t Depth = sizeof...(Offsets);
    static constexpr uint64_t Offs[Depth] = { Offsets... };
};

template<typename Chain>
class PtrResolver {
    static constexpr size_t D = Chain::Depth;
    struct { uint64_t addr; uint32_t epoch; uint8_t crc; } cache_[D];
    uint32_t epoch_ = 1;
    uint64_t mod_base_ = 0;
    uint64_t phys_offset_ = 0;
    uint64_t (*read64_)(uint64_t) = nullptr;

    static uint8_t crc8(uint64_t v) noexcept {
        uint8_t c = 0;
        for (int i = 0; i < 8; ++i) {
            c ^= (uint8_t)(v >> (i * 8));
            c = (c << 1) ^ ((c & 0x80) ? 0x07 : 0);
        }
        return c;
    }

public:
    void init(uint64_t (*read64)(uint64_t)) noexcept {
        read64_ = read64;
    }

    void set_base(uint64_t virt, uint64_t phys) noexcept {
        mod_base_ = virt;
        phys_offset_ = phys - virt;
        ++epoch_;
        __builtin_memset(cache_, 0, sizeof(cache_));
    }

    uint64_t resolve() noexcept {
        uint64_t addr = mod_base_;
        for (size_t i = 0; i < D; ++i) {
            uint64_t target = addr + Chain::Offs[i];

            if (cache_[i].epoch == epoch_ && cache_[i].crc == crc8(target)) {
                addr = cache_[i].addr;
                continue;
            }

            if (i == D - 1 && Chain::Offs[i] < 0x1000) {
                addr = target;
                break;
            }

            uint64_t ptr = read64_(target);
            if (!ptr || ptr == UINT64_MAX) return 0;

            addr = ptr;
            cache_[i].addr = addr;
            cache_[i].epoch = epoch_;
            cache_[i].crc = crc8(target);
        }
        return addr;
    }
};

struct MEM_ENTRY {
    uint64_t base; uint32_t size; uint32_t pad;
    wchar_t name[32];
};

class ModuleDB {
    MEM_ENTRY* entries_ = nullptr;
    uint32_t count_ = 0;
    uint64_t (*read_)(uint64_t, void*, uint32_t) = nullptr;
    uint64_t dtb_ = 0;

    uint64_t read_ptr(uint64_t addr) noexcept {
        uint64_t v = 0;
        return read_(addr, &v, 8) ? v : 0;
    }

public:
    void init(uint64_t (*dma_read)(uint64_t, void*, uint32_t), uint64_t dtb) noexcept {
        read_ = dma_read; dtb_ = dtb;
    }

    uint64_t find_module(const wchar_t* name) noexcept {
        if (!dtb_) return 0;
        uint64_t ep = dtb_;
        uint64_t peb = read_ptr(ep + 0x550);
        if (!peb) return 0;
        uint64_t ldr = read_ptr(peb + 0x018);
        if (!ldr) return 0;

        uint64_t flink = read_ptr(ldr + 0x010);
        uint64_t head = flink;
        wchar_t buf[64];

        do {
            uint64_t entry = flink - 0x010;
            uint64_t dll_base = read_ptr(entry + 0x030);
            if (!dll_base) continue;

            uint64_t name_ptr = read_ptr(entry + 0x048);
            uint32_t name_len = 0;
            read_(entry + 0x048, &name_len, 4);
            uint32_t chars = (name_len / 2) < 32 ? (name_len / 2) : 31;

            if (chars && name_ptr) {
                read_(name_ptr, buf, chars * 2);
                buf[chars] = 0;
                bool match = true;
                for (uint32_t i = 0; i < chars && name[i]; ++i) {
                    if ((buf[i] | 0x20) != (name[i] | 0x20)) { match = false; break; }
                }
                if (match && !name[chars]) return dll_base;
            }

            flink = read_ptr(flink);
            if (!flink || flink == head) break;
        } while (true);
        return 0;
    }
};

extern "C" {
    __declspec(dllexport) uint64_t __stdcall PtrResolve(void* res, uint64_t base, uint64_t phys);
    __declspec(dllexport) uint64_t __stdcall ModuleFind(void* db, const wchar_t* name);
}

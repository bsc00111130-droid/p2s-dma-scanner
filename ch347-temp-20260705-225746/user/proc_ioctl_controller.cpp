#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cctype>

#pragma comment(lib, "ntdll.lib")

#define IOC_MTH(code,mth) CTL_CODE(0x8320, code, mth, FILE_READ_DATA | FILE_WRITE_DATA)
#define IOC_CTL(code) IOC_MTH(code, METHOD_NEITHER)
#define IOC_BUF(code) IOC_MTH(code, METHOD_BUFFERED)

#define IOCTL_MAP     IOC_CTL(0x811)
#define IOCTL_UNMAP   IOC_CTL(0x812)
#define IOCTL_BATCH   IOC_CTL(0x814)
#define IOCTL_WRITE   IOC_CTL(0x815)
#define IOCTL_CHAIN   IOC_BUF(0x816)
#define IOCTL_SCAN    IOC_BUF(0x817)
#define IOCTL_MODULE  IOC_BUF(0x818)
#define IOCTL_MON     IOC_CTL(0x819)
#define IOCTL_UNMON   IOC_CTL(0x81A)
#define MAX_BATCH 32
#define MAX_CHAIN 16

#pragma pack(push, 1)
/* MAP */
typedef struct { unsigned long pid; unsigned long flags; unsigned long long va; SIZE_T sz; } MREQ;
typedef struct { unsigned long long uva; SIZE_T msz; unsigned long long pa; long st; } MRSP;
/* BATCH */
typedef struct { unsigned long long va; SIZE_T sz; } BENT;
typedef struct { unsigned long pid; unsigned long flags; unsigned long cnt; BENT e[MAX_BATCH]; } BREQ;
typedef struct { unsigned long cnt; long st; unsigned long long uva[MAX_BATCH]; unsigned long long pa[MAX_BATCH]; SIZE_T msz[MAX_BATCH]; } BRSP;
/* CHAIN */
typedef struct { unsigned long pid; unsigned long flags; unsigned long long mod; unsigned long depth; unsigned long long offs[MAX_CHAIN]; } CREQ;
typedef struct { unsigned long long fin; long st; unsigned long long lvls[MAX_CHAIN]; } CRSP;
/* WRITE */
typedef struct { unsigned long pid; unsigned long flags; unsigned long long va; unsigned long sz; unsigned char data[0x1000]; } WREQ;
/* SCAN */
typedef struct { unsigned char v; unsigned char m; } PBT;
typedef struct { unsigned long pid; unsigned long flags; unsigned long long sa; unsigned long long ea; unsigned long plen; unsigned long _pad; PBT pat[64]; unsigned long max; } SREQ;
typedef struct { unsigned long long addr; unsigned long idx; } SRES;
typedef struct { unsigned long cnt; SRES res[256]; } SRSP;
/* MODULE */
typedef struct { unsigned long pid; wchar_t name[64]; } MODREQ;
typedef struct { unsigned long long base; unsigned long long sz; unsigned long long ep; long st; } MODRSP;
#pragma pack(pop)

static HANDLE g_dev = INVALID_HANDLE_VALUE;

static HANDLE OpenDev() {
    return CreateFileW(L"\\\\.\\P2S", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

static MRSP DoMap(DWORD pid, ULONG_PTR va, SIZE_T sz, ULONG flags) {
    MREQ r = { pid, flags, va, sz }; MRSP s = {}; DWORD ret;
    DeviceIoControl(g_dev, IOCTL_MAP, &r, sizeof(r), &s, sizeof(s), &ret, NULL);
    return s;
}

static void DoUnmap(ULONG_PTR uva) {
    DWORD ret; DeviceIoControl(g_dev, IOCTL_UNMAP, (void*)uva, 8, NULL, 0, &ret, NULL);
}

static MODRSP DoModule(DWORD pid, const wchar_t* name) {
    MODREQ r = { pid }; wcscpy_s(r.name, 64, name); MODRSP s = {}; DWORD ret;
    DeviceIoControl(g_dev, IOCTL_MODULE, &r, sizeof(r), &s, sizeof(s), &ret, NULL);
    return s;
}

static CRSP DoChain(DWORD pid, ULONG_PTR mod, ULONG depth, ULONG_PTR* offs) {
    CREQ r = { pid, 0, mod, depth }; CRSP s = {}; DWORD ret;
    for (ULONG i = 0; i < depth && i < MAX_CHAIN; i++) r.offs[i] = offs[i];
    DeviceIoControl(g_dev, IOCTL_CHAIN, &r, sizeof(r), &s, sizeof(s), &ret, NULL);
    return s;
}

static SRSP DoScan(DWORD pid, ULONG_PTR sa, ULONG_PTR ea, UCHAR* pat, ULONG plen, ULONG max) {
    SREQ r = { pid, 0, sa, ea, plen, 0, {}, max }; SRSP s = {}; DWORD ret;
    for (ULONG i = 0; i < plen && i < 64; i++) { r.pat[i].v = pat[i*2]; r.pat[i].m = pat[i*2+1]; }
    DeviceIoControl(g_dev, IOCTL_SCAN, &r, sizeof(r), &s, sizeof(s), &ret, NULL);
    return s;
}

static void DoWrite(DWORD pid, ULONG_PTR va, void* data, ULONG sz) {
    WREQ r = { pid, 0, va, sz }; if (sz > 0x1000) sz = 0x1000;
    memcpy(r.data, data, sz); DWORD ret;
    DeviceIoControl(g_dev, IOCTL_WRITE, &r, sizeof(r), NULL, 0, &ret, NULL);
}

/* AOB parser: "48 8B 05 ? ? ? ?" ??UCHAR pairs {value, mask} */
static ULONG ParseAOB(const char* text, UCHAR* out_pairs, ULONG max) {
    ULONG n = 0; const char* p = text;
    while (*p && n < max) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (*p == '?') {
            p++; if (*p == '?') p++;
            out_pairs[n*2] = 0x00; out_pairs[n*2+1] = 0x00; n++;
        } else {
            char h[3] = { p[0], p[1], 0 };
            if (h[0] && h[1]) { out_pairs[n*2] = (UCHAR)strtoul(h, NULL, 16); out_pairs[n*2+1] = 0xFF; n++; p += 2; }
            else break;
        }
    }
    return n;
}

static void HexDump(void* data, ULONG sz) {
    UCHAR* d = (UCHAR*)data;
    for (ULONG i = 0; i < sz; i += 16) {
        printf("%08lx: ", i);
        for (ULONG j = 0; j < 16 && i + j < sz; j++) printf("%02x ", d[i + j]);
        for (ULONG j = sz - i; j < 16; j++) printf("   ");
        for (ULONG j = 0; j < 16 && i + j < sz; j++) printf("%c", (d[i+j] >= 32 && d[i+j] < 127) ? d[i+j] : '.');
        printf("\n");
    }
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) {
        printf("P2S Memory Access Tool\n"
               "Usage: p2s <cmd> [args]\n\n"
               "  resolve <pid> <module>     Resolve module base address\n"
               "  chain   <pid> <modbase> <off1> [off2...]  Walk pointer chain\n"
               "  read    <pid> <va> <size>   Map and read memory (hexdump)\n"
               "  write   <pid> <va> <hex>    Write bytes to process memory\n"
               "  scan    <pid> <start> <end> <AOB>  Pattern scan\n"
               "  batch   <pid> <va:sz> ...    Batch map multiple regions\n"
               "  monitor <pid>                Register process death callback\n"
               "  dump    <pid> <va> <size>    Full hex dump\n");
        return 1;
    }

    g_dev = OpenDev();
    if (g_dev == INVALID_HANDLE_VALUE) { printf("[!] Open \\\\.\\P2S failed\n"); return 1; }

    DWORD pid = (argc > 2) ? wcstoul(argv[2], NULL, 10) : 0;
    const wchar_t* cmd = argv[1];

    if (wcscmp(cmd, L"resolve") == 0 && argc >= 4) {
        MODRSP r = DoModule(pid, argv[3]);
        if (r.st == 0) printf("base=0x%llx  size=%llu  entry=0x%llx\n", r.base, r.sz, r.ep);
        else printf("[!] resolve failed: %ld\n", r.st);
    }
    else if (wcscmp(cmd, L"chain") == 0 && argc >= 5) {
        ULONG_PTR mod = _wcstoui64(argv[3], NULL, 16);
        ULONG dep = (ULONG)(argc - 4); if (dep > MAX_CHAIN) dep = MAX_CHAIN;
        ULONG_PTR offs[MAX_CHAIN]; for (ULONG i = 0; i < dep; i++) offs[i] = _wcstoui64(argv[4+i], NULL, 16);
        CRSP r = DoChain(pid, mod, dep, offs);
        printf("final=0x%llx  status=%ld\n", r.fin, r.st);
        for (ULONG i = 0; i < dep; i++) printf("  L%u: +0x%llx ??0x%llx\n", i, offs[i], r.lvls[i]);
        if (r.st == 0) {
            MRSP m = DoMap(pid, (ULONG_PTR)r.fin, 64, 0);
            if (m.st == 0) { HexDump((void*)(ULONG_PTR)m.uva, 64); DoUnmap((ULONG_PTR)m.uva); }
        }
    }
    else if ((wcscmp(cmd, L"read") == 0 || wcscmp(cmd, L"dump") == 0) && argc >= 5) {
        ULONG_PTR va = _wcstoui64(argv[3], NULL, 16);
        SIZE_T sz = (SIZE_T)_wcstoui64(argv[4], NULL, 10);
        MRSP m = DoMap(pid, va, sz, 0);
        if (m.st == 0) {
            ULONG show = (ULONG)(sz > 1024 ? 1024 : sz);
            printf("uva=0x%llx  pa=0x%llx  size=%llu\n", m.uva, m.pa, m.msz);
            HexDump((void*)(ULONG_PTR)m.uva, show);
            if (sz > 1024) printf("... (%llu more bytes)\n", (ULONGLONG)(sz - 1024));
            DoUnmap((ULONG_PTR)m.uva);
        } else printf("[!] map failed: %ld\n", m.st);
    }
    else if (wcscmp(cmd, L"scan") == 0 && argc >= 6) {
        ULONG_PTR sa = _wcstoui64(argv[3], NULL, 16);
        ULONG_PTR ea = _wcstoui64(argv[4], NULL, 16);
        char aob[512]; wcstombs(aob, argv[5], 511);
        UCHAR pairs[128]; ULONG plen = ParseAOB(aob, pairs, 64);
        if (plen == 0) { printf("[!] invalid AOB pattern\n"); return 1; }
        SRSP r = DoScan(pid, sa, ea, pairs, plen, 256);
        printf("found %lu results:\n", r.cnt);
        for (ULONG i = 0; i < r.cnt && i < 16; i++)
            printf("  [%lu] 0x%llx\n", i, r.res[i].addr);
        if (r.cnt > 16) printf("  ... %lu more\n", r.cnt - 16);
    }
    else if (wcscmp(cmd, L"write") == 0 && argc >= 5) {
        ULONG_PTR va = _wcstoui64(argv[3], NULL, 16);
        char hex[2048]; wcstombs(hex, argv[4], 2047);
        ULONG blen = (ULONG)(strlen(hex) / 2);
        UCHAR* buf = (UCHAR*)malloc(blen + 1);
        for (ULONG i = 0; i < blen; i++) { char h[3] = { hex[i*2], hex[i*2+1], 0 }; buf[i] = (UCHAR)strtoul(h, NULL, 16); }
        DoWrite(pid, va, buf, blen);
        printf("wrote %lu bytes to 0x%llx\n", blen, va);
        free(buf);
    }
    else if (wcscmp(cmd, L"batch") == 0 && argc >= 4) {
        BREQ r = { pid, 0, 0 }; BRSP s = {};
        ULONG n = (ULONG)(argc - 3); if (n > MAX_BATCH) n = MAX_BATCH;
        for (ULONG i = 0; i < n; i++) {
            wchar_t* sep = wcschr(argv[3+i], L':'); if (!sep) continue;
            *sep = 0;
            r.e[i].va = _wcstoui64(argv[3+i], NULL, 16);
            r.e[i].sz = (SIZE_T)_wcstoui64(sep+1, NULL, 10);
            r.cnt++;
        }
        DWORD ret;
        DeviceIoControl(g_dev, IOCTL_BATCH, &r, sizeof(r), &s, sizeof(s), &ret, NULL);
        printf("mapped %lu regions:\n", s.cnt);
        for (ULONG i = 0; i < s.cnt; i++)
            printf("  [%lu] uva=0x%llx  pa=0x%llx  size=%llu\n", i, s.uva[i], s.pa[i], s.msz[i]);
    }
    else if (wcscmp(cmd, L"monitor") == 0) {
        DWORD ret;
        BOOL ok = DeviceIoControl(g_dev, IOCTL_MON, &pid, 4, NULL, 0, &ret, NULL);
        printf("monitor %s\n", ok ? "OK" : "FAIL");
    }
    else {
        printf("[!] unknown command\n");
    }

    CloseHandle(g_dev);
    return 0;
}

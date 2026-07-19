#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <tlhelp32.h>
#include <commctrl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>
#include <thread>
#include <atomic>

#pragma comment(lib, "comctl32.lib")

/* IOCTL */
#define IOC_MTH(c,m) CTL_CODE(0x8320, c, m, FILE_READ_DATA | FILE_WRITE_DATA)
#define IOCTL_MAP    IOC_MTH(0x811, 3)
#define IOCTL_UNMAP  IOC_MTH(0x812, 3)
#define IOCTL_CHAIN  IOC_MTH(0x816, 0)
#define IOCTL_SCAN   IOC_MTH(0x817, 0)
#define IOCTL_MODULE IOC_MTH(0x818, 0)
#define IOCTL_WRITE  IOC_MTH(0x815, 3)

#pragma pack(push,1)
typedef struct { uint32_t pid; uint32_t f; uint64_t va; size_t sz; } MREQ;
typedef struct { uint64_t uva; size_t msz; uint64_t pa; int32_t st; } MRSP;
typedef struct { uint32_t pid; uint32_t f; uint64_t mod; uint32_t d; uint64_t offs[16]; } CREQ;
typedef struct { uint64_t fin; int32_t st; uint64_t lvls[16]; } CRSP;
typedef struct { uint32_t pid; wchar_t n[64]; } MODREQ;
typedef struct { uint64_t base; uint64_t sz; uint64_t ep; int32_t st; } MODRSP;
typedef struct { uint8_t v; uint8_t m; } PBT;
typedef struct { uint32_t pid; uint32_t f; uint64_t sa; uint64_t ea; uint32_t plen; uint32_t _;
    union { PBT pat[64]; int32_t si; float sf; int64_t sl; wchar_t ss[32]; };
    uint32_t max; uint32_t _p2; uint64_t exc; } SREQ;
typedef struct { uint64_t addr; uint32_t idx; } SRES;
typedef struct { uint32_t cnt; SRES res[256]; } SRSP;
typedef struct { uint32_t pid; uint32_t f; uint64_t va; uint32_t sz; uint8_t d[4096]; } WREQ;
#pragma pack(pop)

#define ID_BTN_PROC   101
#define ID_BTN_RES    102
#define ID_BTN_CHAIN  103
#define ID_BTN_READ   104
#define ID_BTN_SCAN   105
#define ID_BTN_WRITE  106
#define ID_BTN_WATCH  107
#define ID_BTN_CLEAR  108
#define ID_LIST_RES   201
#define ID_LIST_DUMP  202
#define ID_EDIT_ADDR  301
#define ID_EDIT_OFFS  302
#define ID_EDIT_AOB   303
#define ID_EDIT_VAL   304
#define ID_EDIT_DUMP  305
#define ID_EDIT_LOG   306
#define ID_EDIT_SKIP  307
#define ID_COMBO_PROC 401
#define ID_COMBO_TYPE 402
#define ID_TIMER      501

static HINSTANCE hInst;
static HWND hWnd, hLog, hProc, hRes, hDump;
static HWND hAddr, hOffs, hAob, hVal, hSkip;
static HWND hType, hBtnRes, hBtnChain, hBtnRead, hBtnScan, hBtnWrite, hBtnWatch;
static HANDLE gDev = INVALID_HANDLE_VALUE;
static uint32_t gPid = 0;
static std::atomic<bool> gWatch{false};

static void LOG(const wchar_t* fmt, ...) {
    wchar_t b[8192]; va_list ap; va_start(ap, fmt); vswprintf(b, 8192, fmt, ap); va_end(ap);
    wcscat(b, L"\r\n"); int len = GetWindowTextLengthW(hLog);
    SendMessageW(hLog, EM_SETSEL, len, len); SendMessageW(hLog, EM_REPLACESEL, 0, (LPARAM)b);
}

static HANDLE OpenDev() {
    return CreateFileW(L"\\\\.\\P2S", GENERIC_READ|GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
}

static void EnumProcs() {
    SendMessageW(hProc, CB_RESETCONTENT, 0, 0);
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W pe = { sizeof(pe) };
    if (Process32FirstW(s, &pe)) do {
        int i = (int)SendMessageW(hProc, CB_ADDSTRING, 0, (LPARAM)pe.szExeFile);
        SendMessageW(hProc, CB_SETITEMDATA, i, pe.th32ProcessID);
    } while (Process32NextW(s, &pe));
    CloseHandle(s); SendMessageW(hProc, CB_SETCURSEL, 0, 0);
}

static uint32_t GetPid() {
    int s = (int)SendMessageW(hProc, CB_GETCURSEL, 0, 0);
    return s < 0 ? 0 : (uint32_t)SendMessageW(hProc, CB_GETITEMDATA, s, 0);
}

static uint64_t GetVa(HWND h) {
    wchar_t t[128]; GetWindowTextW(h, t, 128);
    return _wcstoui64(t, NULL, 16);
}

static void DoResolve() {
    gPid = GetPid(); if (!gPid) return;
    wchar_t name[64]; SendMessageW(hProc, CB_GETLBTEXT, SendMessageW(hProc, CB_GETCURSEL,0,0), (LPARAM)name);
    MODREQ r = { gPid }; wcscpy_s(r.n, 64, name); MODRSP s = {}; DWORD ret;
    if (DeviceIoControl(gDev, IOCTL_MODULE, &r, sizeof(r), &s, sizeof(s), &ret, NULL) && s.st == 0) {
        wchar_t t[64]; swprintf(t, 64, L"0x%llx", s.base); SetWindowTextW(hAddr, t);
        LOG(L"[OK] %ls base=0x%llx", name, s.base);
    } else LOG(L"[!] resolve fail");
}

static void DoChain() {
    gPid = GetPid(); if (!gPid) return;
    uint64_t mod = GetVa(hAddr); wchar_t tmp[256]; GetWindowTextW(hOffs, tmp, 256);
    if (!mod || !tmp[0]) return;
    std::vector<uint64_t> o; wchar_t* c; wchar_t* t = wcstok_s(tmp, L" ,", &c);
    while (t) { o.push_back(_wcstoui64(t, NULL, 16)); t = wcstok_s(NULL, L" ,", &c); }
    if (o.empty()) return;
    CREQ r = { gPid, 0, mod, (uint32_t)o.size() }; CRSP s = {}; DWORD ret;
    for (size_t i = 0; i < o.size() && i < 16; i++) r.offs[i] = o[i];
    if (DeviceIoControl(gDev, IOCTL_CHAIN, &r, sizeof(r), &s, sizeof(s), &ret, NULL) && s.st == 0) {
        LOG(L"[OK] chain 0x%llx", s.fin);
        swprintf(tmp, 256, L"0x%llx", s.fin); SetWindowTextW(hAddr, tmp);
        MREQ mr = { gPid, 0, s.fin, 64 }; MRSP ms = {};
        DeviceIoControl(gDev, IOCTL_MAP, &mr, sizeof(mr), &ms, sizeof(ms), &ret, NULL);
        if (ms.st == 0) {
            uint8_t* d = (uint8_t*)(ULONG_PTR)ms.uva;
            LOG(L"[dump] %02x %02x %02x %02x %02x %02x %02x %02x...", d[0],d[1],d[2],d[3],d[4],d[5],d[6],d[7]);
            DeviceIoControl(gDev, IOCTL_UNMAP, (void*)(ULONG_PTR)ms.uva, 8, NULL, 0, &ret, NULL);
        }
    } else LOG(L"[!] chain fail %ld", s.st);
}

static void DoRead() {
    gPid = GetPid(); uint64_t va = GetVa(hAddr); if (!gPid || !va) return;
    MREQ mr = { gPid, 0, va, 64 }; MRSP ms = {}; DWORD ret;
    if (DeviceIoControl(gDev, IOCTL_MAP, &mr, sizeof(mr), &ms, sizeof(ms), &ret, NULL) && ms.st == 0) {
        uint8_t* d = (uint8_t*)(ULONG_PTR)ms.uva;
        wchar_t line[256]; line[0] = 0;
        for (int i = 0; i < 64; i++) { wchar_t b[8]; swprintf(b, 8, L"%02x ", d[i]); wcscat(line, b); }
        LOG(L"[0x%llx] %ls", va, line);
        DeviceIoControl(gDev, IOCTL_UNMAP, (void*)(ULONG_PTR)ms.uva, 8, NULL, 0, &ret, NULL);
    } else LOG(L"[!] read fail");
}

static void DoWrite() {
    gPid = GetPid(); uint64_t va = GetVa(hAddr); if (!gPid || !va) return;
    wchar_t tmp[4096]; GetWindowTextW(hAob, tmp, 4096);
    char hex[4096]; wcstombs(hex, tmp, 4095);
    WREQ r = { gPid, 0, va, 0 }; int blen = (int)strlen(hex)/2;
    if (blen > 4096) blen = 4096; r.sz = blen;
    for (int i = 0; i < blen; i++) { char h[3]={hex[i*2],hex[i*2+1],0}; r.d[i]=(uint8_t)strtoul(h,0,16); }
    DWORD ret; DeviceIoControl(gDev, IOCTL_WRITE, &r, sizeof(r), NULL, 0, &ret, NULL);
    LOG(L"[OK] wrote %d bytes", blen);
}

static void DoScan() {
    gPid = GetPid(); if (!gPid) { LOG(L"[!] select process"); return; }
    uint64_t end = 0x7FFFFFFF; wchar_t tmp[256];
    GetWindowTextW(hAob, tmp, 256); char aob[512]; wcstombs(aob, tmp, 511);
    int type = (int)SendMessageW(hType, CB_GETCURSEL, 0, 0);
    GetWindowTextW(hVal, tmp, 256);
    uint64_t skip = GetVa(hSkip);

    SREQ r = { gPid, (uint32_t)type, 0, end, 0, 0, {}, 256, 0, skip };
    ULONG plen = 0;
    if (type == 0) {
        const char* p = aob; while (*p && plen < 64) {
            while (*p == ' ') p++; if (!*p) break;
            if (*p == '?') { p+=2; r.pat[plen].v=0; r.pat[plen].m=0; plen++; }
            else { char h[3]={p[0],p[1],0}; r.pat[plen].v=(uint8_t)strtoul(h,0,16); r.pat[plen].m=0xFF; plen++; p+=2; }
        } r.plen = plen;
    } else if (type == 1 || type == 2) {
        char v[64]; wcstombs(v, tmp, 63);
        if (type == 1) r.si = (int32_t)strtol(v, NULL, 0);
        else r.sf = (float)strtod(v, NULL);
    }
    if (type == 0 && !plen) { LOG(L"[!] invalid AOB"); return; }

    DWORD ret; SRSP s = {};
    if (!DeviceIoControl(gDev, IOCTL_SCAN, &r, sizeof(r), &s, sizeof(s), &ret, NULL)) {
        LOG(L"[!] scan IOCTL failed"); return;
    }
    LOG(L"[OK] %lu results", s.cnt);
    SendMessageW(hRes, LB_RESETCONTENT, 0, 0);
    for (uint32_t i = 0; i < s.cnt && i < 256; i++) {
        wchar_t line[64]; swprintf(line, 64, L"[%u] 0x%llx", i, s.res[i].addr);
        SendMessageW(hRes, LB_ADDSTRING, 0, (LPARAM)line);
    }
}

/* Watch thread: continuously reads the address and logs changes */
static void WatchThread(uint64_t va) {
    if (!gDev || gDev == INVALID_HANDLE_VALUE) return;
    gWatch = true; uint64_t last = 0; uint32_t seq = 0;
    while (gWatch) {
        MREQ mr = { gPid, 0, va, 8 }; MRSP ms = {}; DWORD ret;
        if (DeviceIoControl(gDev, IOCTL_MAP, &mr, sizeof(mr), &ms, sizeof(ms), &ret, NULL) && ms.st == 0) {
            uint64_t val = *(volatile uint64_t*)(ULONG_PTR)ms.uva;
            if (val != last) {
                LOG(L"[WATCH#%u] 0x%llx -> 0x%llx", ++seq, last, val);
                last = val;
            }
            DeviceIoControl(gDev, IOCTL_UNMAP, (void*)(ULONG_PTR)ms.uva, 8, NULL, 0, &ret, NULL);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE: {
        int y = 10;
        hProc = CreateWindowW(L"COMBOBOX", 0, WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST, 10,y,220,200, h, (HMENU)ID_COMBO_PROC, hInst, 0);
        CreateWindowW(L"BUTTON", L"Resolve", WS_CHILD|WS_VISIBLE, 235,y,60,24, h, (HMENU)ID_BTN_RES, hInst, 0);
        CreateWindowW(L"BUTTON", L"Chain", WS_CHILD|WS_VISIBLE, 300,y,50,24, h, (HMENU)ID_BTN_CHAIN, hInst, 0);
        CreateWindowW(L"BUTTON", L"Read", WS_CHILD|WS_VISIBLE, 355,y,50,24, h, (HMENU)ID_BTN_READ, hInst, 0);
        CreateWindowW(L"BUTTON", L"Watch", WS_CHILD|WS_VISIBLE, 410,y,50,24, h, (HMENU)ID_BTN_WATCH, hInst, 0);
        y += 28;
        CreateWindowW(L"STATIC", L"Addr:", WS_CHILD|WS_VISIBLE, 10,y,35,20, h, 0, hInst, 0);
        hAddr = CreateWindowW(L"EDIT", L"0x0", WS_CHILD|WS_VISIBLE|WS_BORDER|ES_RIGHT, 45,y,150,20, h, (HMENU)ID_EDIT_ADDR, hInst, 0);
        CreateWindowW(L"STATIC", L"Off:", WS_CHILD|WS_VISIBLE, 200,y,25,20, h, 0, hInst, 0);
        hOffs = CreateWindowW(L"EDIT", L"", WS_CHILD|WS_VISIBLE|WS_BORDER, 225,y,200,20, h, (HMENU)ID_EDIT_OFFS, hInst, 0);
        CreateWindowW(L"BUTTON", L"Write", WS_CHILD|WS_VISIBLE, 430,y,50,20, h, (HMENU)ID_BTN_WRITE, hInst, 0);
        CreateWindowW(L"BUTTON", L"Clear", WS_CHILD|WS_VISIBLE, 485,y,50,20, h, (HMENU)ID_BTN_CLEAR, hInst, 0);
        y += 24;
        hType = CreateWindowW(L"COMBOBOX", 0, WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST, 10,y,80,100, h, (HMENU)ID_COMBO_TYPE, hInst, 0);
        SendMessageW(hType, CB_ADDSTRING, 0, (LPARAM)L"AOB");
        SendMessageW(hType, CB_ADDSTRING, 0, (LPARAM)L"Int32");
        SendMessageW(hType, CB_ADDSTRING, 0, (LPARAM)L"Float");
        SendMessageW(hType, CB_SETCURSEL, 0, 0);
        hAob = CreateWindowW(L"EDIT", L"", WS_CHILD|WS_VISIBLE|WS_BORDER, 95,y,240,20, h, (HMENU)ID_EDIT_AOB, hInst, 0);
        CreateWindowW(L"STATIC", L"Val:", WS_CHILD|WS_VISIBLE, 340,y,30,20, h, 0, hInst, 0);
        hVal = CreateWindowW(L"EDIT", L"0", WS_CHILD|WS_VISIBLE|WS_BORDER, 370,y,80,20, h, (HMENU)ID_EDIT_VAL, hInst, 0);
        CreateWindowW(L"STATIC", L"Skip:", WS_CHILD|WS_VISIBLE, 455,y,30,20, h, 0, hInst, 0);
        hSkip = CreateWindowW(L"EDIT", L"0", WS_CHILD|WS_VISIBLE|WS_BORDER, 485,y,80,20, h, (HMENU)ID_EDIT_SKIP, hInst, 0);
        CreateWindowW(L"BUTTON", L"Scan", WS_CHILD|WS_VISIBLE, 570,y,50,20, h, (HMENU)ID_BTN_SCAN, hInst, 0);
        y += 24;
        hRes  = CreateWindowW(L"LISTBOX", 0, WS_CHILD|WS_VISIBLE|WS_BORDER|WS_VSCROLL, 10,y,350,150, h, (HMENU)ID_LIST_RES, hInst, 0);
        hDump = CreateWindowW(L"EDIT", 0, WS_CHILD|WS_VISIBLE|WS_BORDER|ES_MULTILINE|ES_READONLY|WS_VSCROLL, 365,y,350,150, h, (HMENU)ID_LIST_DUMP, hInst, 0);
        y += 154;
        hLog  = CreateWindowW(L"EDIT", 0, WS_CHILD|WS_VISIBLE|WS_BORDER|ES_MULTILINE|ES_READONLY|WS_VSCROLL|ES_AUTOVSCROLL, 10,y,710,200, h, (HMENU)ID_EDIT_LOG, hInst, 0);
        SendMessageW(hLog, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), 0);
        SendMessageW(hDump, WM_SETFONT, (WPARAM)GetStockObject(DEFAULT_GUI_FONT), 0);
        EnumProcs(); gDev = OpenDev();
        LOG(L"[+] P2S Memory Scanner ready%s", gDev != INVALID_HANDLE_VALUE ? L"" : L" (driver offline)");
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id == ID_BTN_RES) { DoResolve(); break; }
        if (id == ID_BTN_CHAIN) { DoChain(); break; }
        if (id == ID_BTN_READ) { DoRead(); break; }
        if (id == ID_BTN_SCAN) { DoScan(); break; }
        if (id == ID_BTN_WRITE) { DoWrite(); break; }
        if (id == ID_BTN_CLEAR) { SendMessageW(hLog, WM_SETTEXT, 0, 0); break; }
        if (id == ID_BTN_WATCH) {
            uint64_t va = GetVa(hAddr);
            if (va) std::thread(WatchThread, va).detach();
            break;
        }
        if (id == ID_LIST_RES && HIWORD(w) == LBN_DBLCLK) {
            int sel = (int)SendMessageW(hRes, LB_GETCURSEL, 0, 0);
            if (sel < 0) break;
            wchar_t line[64]; SendMessageW(hRes, LB_GETTEXT, sel, (LPARAM)line);
            wchar_t* a = wcschr(line, L'x'); if (!a) break;
            uint64_t va = _wcstoui64(a, NULL, 16);
            wchar_t t[32]; swprintf(t, 32, L"0x%llx", va); SetWindowTextW(hAddr, t);
            MREQ mr = { GetPid(), 0, va, 256 }; MRSP ms = {}; DWORD ret;
            if (DeviceIoControl(gDev, IOCTL_MAP, &mr, sizeof(mr), &ms, sizeof(ms), &ret, NULL) && ms.st == 0) {
                uint8_t* d = (uint8_t*)(ULONG_PTR)ms.uva;
                wchar_t dump[8192]; dump[0] = 0;
                for (int i = 0; i < 256; i++) { wchar_t b[8]; swprintf(b,8,L"%02x ",d[i]); wcscat(dump,b);
                    if ((i+1)%16==0) wcscat(dump,L"\r\n"); }
                SetWindowTextW(hDump, dump);
                DeviceIoControl(gDev, IOCTL_UNMAP, (void*)(ULONG_PTR)ms.uva, 8, NULL, 0, &ret, NULL);
            }
            break;
        }
        break;
    }
    case WM_DESTROY: { gWatch = false; if (gDev != INVALID_HANDLE_VALUE) CloseHandle(gDev); PostQuitMessage(0); return 0; }
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, LPWSTR, int) {
    hInst = hi;
    INITCOMMONCONTROLSEX ic = { sizeof(ic), ICC_WIN95_CLASSES }; InitCommonControlsEx(&ic);
    WNDCLASSW wc = {0}; wc.lpfnWndProc = WndProc; wc.hInstance = hi;
    wc.hCursor = LoadCursorA(0, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszClassName = L"P2SW"; RegisterClassW(&wc);
    hWnd = CreateWindowW(L"P2SW", L"P2S Memory Scanner", WS_OVERLAPPEDWINDOW|WS_VISIBLE,
                         CW_USEDEFAULT, CW_USEDEFAULT, 750, 520, NULL, NULL, hi, NULL);
    if (!hWnd) return 1;
    MSG msg; while (GetMessageW(&msg,0,0,0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return 0;
}

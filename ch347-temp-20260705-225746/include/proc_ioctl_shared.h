#pragma once
#include <basetsd.h>
#ifndef CTL_CODE
#define CTL_CODE(dev,func,meth,acc) ((dev)<<16|(acc)<<14|(func)<<2|(meth))
#endif

#pragma pack(push, 1)

#define DEVICE_NAME_NATIVE    L"\\Device\\P2S"
#define SYMBOLIC_LINK_NATIVE  L"\\DosDevices\\P2S"
#define USER_VISIBLE_PATH     L"\\\\.\\P2S"
#define FILE_DEVICE_P2S       0x8320
#define P2S_REJECT_READ_SIZE  0x1000u
#define P2S_MAX_BATCH         32
#define P2S_MAX_CHAIN_DEPTH   16
#define P2S_MAX_PATTERN_LEN   64
#define P2S_MAX_MAPPINGS      256

#define IOC_CTL(code) CTL_CODE(FILE_DEVICE_P2S, code, METHOD_NEITHER, FILE_READ_DATA | FILE_WRITE_DATA)
#define IOC_BUF(code) CTL_CODE(FILE_DEVICE_P2S, code, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)

#define IOCTL_P2S_MAP_PROCESS_MEMORY   IOC_CTL(0x811)
#define IOCTL_P2S_UNMAP_PROCESS_MEMORY IOC_CTL(0x812)
#define IOCTL_P2S_QUERY_MAPPED_ADDRESS IOC_BUF(0x813)
#define IOCTL_P2S_MAP_BATCH            IOC_CTL(0x814)
#define IOCTL_P2S_WRITE_PROCESS_MEMORY IOC_CTL(0x815)
#define IOCTL_P2S_WALK_POINTER_CHAIN   IOC_BUF(0x816)
#define IOCTL_P2S_SCAN_PATTERN         IOC_BUF(0x817)
#define IOCTL_P2S_RESOLVE_MODULE       IOC_BUF(0x818)
#define IOCTL_P2S_MONITOR_PROCESS      IOC_CTL(0x819)
#define IOCTL_P2S_UNMONITOR_PROCESS    IOC_CTL(0x81A)

#define P2S_FLAG_NONCACHED     1
#define P2S_FLAG_WRITECOMBINED 2
#define P2S_FLAG_WRITABLE      4

typedef struct _P2S_MAP_REQUEST {
    unsigned long  ProcessId; unsigned long  Flags;
    unsigned long long TargetVa; SIZE_T Size;
} P2S_MAP_REQUEST, *PP2S_MAP_REQUEST;

typedef struct _P2S_MAP_RESPONSE {
    unsigned long long MappedUserVa; SIZE_T MappedSize;
    unsigned long long PhysicalAddress; long Status;
} P2S_MAP_RESPONSE, *PP2S_MAP_RESPONSE;

typedef struct _P2S_BATCH_ENTRY {
    unsigned long long TargetVa; SIZE_T Size;
} P2S_BATCH_ENTRY, *PP2S_BATCH_ENTRY;

typedef struct _P2S_BATCH_REQUEST {
    unsigned long  ProcessId; unsigned long  Flags;
    unsigned long  EntryCount; unsigned long  _pad;
    P2S_BATCH_ENTRY Entries[P2S_MAX_BATCH];
} P2S_BATCH_REQUEST, *PP2S_BATCH_REQUEST;

typedef struct _P2S_BATCH_RESPONSE {
    unsigned long  MappedCount; unsigned long  _pad;
    long           Status;
    unsigned long long MappedVa[P2S_MAX_BATCH];
    unsigned long long PhysAddr[P2S_MAX_BATCH];
    SIZE_T             MappedSize[P2S_MAX_BATCH];
} P2S_BATCH_RESPONSE, *PP2S_BATCH_RESPONSE;

typedef struct _P2S_CHAIN_REQUEST {
    unsigned long  ProcessId; unsigned long  Flags;
    unsigned long long ModuleBase;
    unsigned long  Depth; unsigned long  _pad;
    unsigned long long Offsets[P2S_MAX_CHAIN_DEPTH];
} P2S_CHAIN_REQUEST, *PP2S_CHAIN_REQUEST;

typedef struct _P2S_CHAIN_RESPONSE {
    unsigned long long ResolvedVa; long Status;
    unsigned long long LevelAddrs[P2S_MAX_CHAIN_DEPTH];
} P2S_CHAIN_RESPONSE, *PP2S_CHAIN_RESPONSE;

typedef struct _P2S_WRITE_REQUEST {
    unsigned long  ProcessId;
    unsigned long  Flags;
    unsigned long long TargetVa;
    unsigned long  Size; unsigned long  _pad;
    unsigned char  Data[0x1000];
} P2S_WRITE_REQUEST, *PP2S_WRITE_REQUEST;

typedef struct _P2S_PATTERN_BYTE {
    unsigned char Value; unsigned char Mask;
} P2S_PATTERN_BYTE, *PP2S_PATTERN_BYTE;

typedef struct _P2S_SCAN_REQUEST {
    unsigned long  ProcessId;
    unsigned long  Flags;          // 0=AOB, 1=value_i32, 2=value_f32, 3=value_i64, 4=string
    unsigned long long StartVa;
    unsigned long long EndVa;
    unsigned long  PatternLen;
    unsigned long  _pad;
    union {
        P2S_PATTERN_BYTE Pattern[P2S_MAX_PATTERN_LEN];
        signed int       ScanInt;
        float            ScanFloat;
        signed long long ScanInt64;
        wchar_t          ScanStr[32];
    };
    unsigned long  MaxResults;
    unsigned long  _pad2;
    unsigned long long ExcludeAddr; // skip this address in results
} P2S_SCAN_REQUEST, *PP2S_SCAN_REQUEST;

typedef struct _P2S_SCAN_RESULT {
    unsigned long long Address; unsigned long  Index; unsigned long  _pad;
} P2S_SCAN_RESULT, *PP2S_SCAN_RESULT;

typedef struct _P2S_SCAN_RESPONSE {
    unsigned long  ResultCount; unsigned long  _pad;
    P2S_SCAN_RESULT Results[256];
} P2S_SCAN_RESPONSE, *PP2S_SCAN_RESPONSE;

typedef struct _P2S_MODULE_REQUEST {
    unsigned long  ProcessId;
    unsigned long  Flags;
    wchar_t        ModuleName[64];
} P2S_MODULE_REQUEST, *PP2S_MODULE_REQUEST;

typedef struct _P2S_MODULE_RESPONSE {
    unsigned long long ModuleBase; unsigned long long ModuleSize;
    unsigned long long EntryPoint; long Status;
} P2S_MODULE_RESPONSE, *PP2S_MODULE_RESPONSE;

typedef struct _P2S_WALK_RESULT {
    unsigned long long FinalVa; long Status;
    unsigned long long Chain[P2S_MAX_CHAIN_DEPTH];
} P2S_WALK_RESULT, *PP2S_WALK_RESULT;

#pragma pack(pop)

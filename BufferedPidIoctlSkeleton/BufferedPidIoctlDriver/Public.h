#pragma once

#ifndef CTL_CODE
#if defined(_KERNEL_MODE) || defined(_NTDDK_) || defined(_WDMDDK_)
#include <ntddk.h>
#else
#include <winioctl.h>
#endif
#endif

#define BUFFERED_PID_IOCTL_DEVICE_TYPE 0x8000

#define IOCTL_BUFFERED_PID_QUERY_PROCESS \
    CTL_CODE(BUFFERED_PID_IOCTL_DEVICE_TYPE, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _BUFFERED_PID_REQUEST {
    unsigned long Size;
    unsigned long Flags;
    unsigned long long ProcessId;
} BUFFERED_PID_REQUEST, *PBUFFERED_PID_REQUEST;

typedef struct _BUFFERED_PID_RESPONSE {
    unsigned long Size;
    long LookupStatus;
    unsigned long Flags;
    unsigned long Reserved;
    unsigned long long ProcessId;
    unsigned char ProcessExists;
    unsigned char ReservedBytes[7];
} BUFFERED_PID_RESPONSE, *PBUFFERED_PID_RESPONSE;

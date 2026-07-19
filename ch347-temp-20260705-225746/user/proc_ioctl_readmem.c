/*
 * proc_ioctl_readmem.c
 *
 * Disabled compatibility placeholder for the old read-memory test client.
 * The active project intentionally keeps arbitrary target-process memory
 * reads disabled.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#include "..\include\proc_ioctl_shared.h"

int wmain(int argc, wchar_t *argv[])
{
    (void)argc;
    (void)argv;

#if PROC_IOCTL_ENABLE_UNSAFE_READMEM
#error PROC_IOCTL_ENABLE_UNSAFE_READMEM must remain disabled for this skeleton.
#endif

    wprintf(L"proc_ioctl_readmem is disabled in this build.\n");
    wprintf(L"No process-memory read IOCTL was sent.\n");
    return ERROR_NOT_SUPPORTED;
}

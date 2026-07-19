#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "..\BufferedPidIoctlDriver\Public.h"

static void PrintLastError(const char* what)
{
    DWORD error = GetLastError();
    fprintf(stderr, "%s failed. GetLastError=%lu\n", what, error);
}

int main(int argc, char** argv)
{
    DWORD pid;
    HANDLE device;
    BUFFERED_PID_REQUEST request;
    BUFFERED_PID_RESPONSE response;
    DWORD bytesReturned;
    BOOL ok;

    if (argc != 2) {
        fprintf(stderr, "usage: UserModeTest.exe <pid>\n");
        return 2;
    }

    pid = strtoul(argv[1], NULL, 10);
    if (pid == 0) {
        fprintf(stderr, "invalid pid\n");
        return 2;
    }

    device = CreateFileW(
        L"\\\\.\\BufferedPidIoctl",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
        );

    if (device == INVALID_HANDLE_VALUE) {
        PrintLastError("CreateFileW");
        return 1;
    }

    ZeroMemory(&request, sizeof(request));
    request.Size = sizeof(request);
    request.ProcessId = (unsigned long long)pid;

    ZeroMemory(&response, sizeof(response));
    bytesReturned = 0;

    ok = DeviceIoControl(
        device,
        IOCTL_BUFFERED_PID_QUERY_PROCESS,
        &request,
        sizeof(request),
        &response,
        sizeof(response),
        &bytesReturned,
        NULL
        );

    if (!ok) {
        PrintLastError("DeviceIoControl");
        CloseHandle(device);
        return 1;
    }

    printf("bytesReturned=%lu\n", bytesReturned);
    printf("response.Size=%lu\n", response.Size);
    printf("response.ProcessId=%llu\n", response.ProcessId);
    printf("response.LookupStatus=0x%08lX\n", (unsigned long)response.LookupStatus);
    printf("response.ProcessExists=%s\n", response.ProcessExists ? "true" : "false");

    CloseHandle(device);
    return 0;
}

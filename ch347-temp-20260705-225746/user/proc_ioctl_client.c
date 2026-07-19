#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>

#include "..\include\proc_ioctl_shared.h"

int
wmain(
    int argc,
    wchar_t **argv
    )
{
    HANDLE device;
    PROC_IOCTL_PID_REQUEST request;
    PROC_IOCTL_PID_RESPONSE response;
    PROC_IOCTL_VALIDATE_READ_REQUEST validateRequest;
    PROC_IOCTL_VALIDATE_READ_RESPONSE validateResponse;
    PROC_IOCTL_LOCK_CALLER_BUFFER_REQUEST lockRequest;
    PROC_IOCTL_LOCK_CALLER_BUFFER_RESPONSE lockResponse;
    DWORD bytesReturned;
    DWORD processId;
    unsigned __int64 address;
    unsigned __int64 parsedSize;
    unsigned char *lockBuffer;
    SIZE_T lockSize;
    SIZE_T index;
    BOOL ok;

    if (argc != 2 && argc != 4 && !(argc == 3 && wcscmp(argv[1], L"lock-self") == 0)) {
        fwprintf(stderr, L"usage:\n");
        fwprintf(stderr, L"  %ls <pid>\n", argv[0]);
        fwprintf(stderr, L"  %ls <pid> <address> <size>\n", argv[0]);
        fwprintf(stderr, L"  %ls lock-self <size>\n", argv[0]);
        return 2;
    }

    if (argc == 3 && wcscmp(argv[1], L"lock-self") == 0) {
        parsedSize = _wcstoui64(argv[2], NULL, 0);
        if (parsedSize == 0 ||
            parsedSize >= PROC_IOCTL_REJECT_READ_SIZE ||
            parsedSize > (unsigned __int64)((SIZE_T)-1)) {
            fwprintf(stderr, L"size must be between 1 and %u bytes\n", PROC_IOCTL_REJECT_READ_SIZE - 1u);
            return 2;
        }

        lockSize = (SIZE_T)parsedSize;
        lockBuffer = (unsigned char *)VirtualAlloc(NULL, lockSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (lockBuffer == NULL) {
            fwprintf(stderr, L"VirtualAlloc failed: %lu\n", GetLastError());
            return 1;
        }

        for (index = 0; index < lockSize; ++index) {
            lockBuffer[index] = (unsigned char)(index & 0xffu);
        }

        device = CreateFileW(
            PROC_IOCTL_USER_PATH,
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL);

        if (device == INVALID_HANDLE_VALUE) {
            fwprintf(stderr, L"CreateFile failed: %lu\n", GetLastError());
            VirtualFree(lockBuffer, 0, MEM_RELEASE);
            return 1;
        }

        ZeroMemory(&lockRequest, sizeof(lockRequest));
        ZeroMemory(&lockResponse, sizeof(lockResponse));

        lockRequest.UserAddress = (unsigned __int64)(ULONG_PTR)lockBuffer;
        lockRequest.Size = lockSize;

        ok = DeviceIoControl(
            device,
            IOCTL_PROC_IOCTL_LOCK_CALLER_BUFFER_ONCE,
            &lockRequest,
            sizeof(lockRequest),
            &lockResponse,
            sizeof(lockResponse),
            &bytesReturned,
            NULL);

        if (!ok) {
            fwprintf(stderr, L"DeviceIoControl failed: %lu\n", GetLastError());
            CloseHandle(device);
            VirtualFree(lockBuffer, 0, MEM_RELEASE);
            return 1;
        }

        wprintf(L"userAddress=0x%I64x size=%Iu pagesLocked=%lu ntstatus=0x%08lx bytes=%lu\n",
            lockResponse.UserAddress,
            lockResponse.RequestedSize,
            lockResponse.PagesLocked,
            (unsigned long)lockResponse.Status,
            bytesReturned);

        CloseHandle(device);
        VirtualFree(lockBuffer, 0, MEM_RELEASE);
        return 0;
    }

    processId = wcstoul(argv[1], NULL, 10);
    if (processId == 0) {
        fwprintf(stderr, L"invalid pid: %ls\n", argv[1]);
        return 2;
    }

    device = CreateFileW(
        PROC_IOCTL_USER_PATH,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (device == INVALID_HANDLE_VALUE) {
        fwprintf(stderr, L"CreateFile failed: %lu\n", GetLastError());
        return 1;
    }

    if (argc == 4) {
        address = _wcstoui64(argv[2], NULL, 0);
        parsedSize = _wcstoui64(argv[3], NULL, 0);

        if (address == 0 || parsedSize == 0 || parsedSize > (unsigned __int64)((SIZE_T)-1)) {
            fwprintf(stderr, L"invalid address or size\n");
            CloseHandle(device);
            return 2;
        }

        ZeroMemory(&validateRequest, sizeof(validateRequest));
        ZeroMemory(&validateResponse, sizeof(validateResponse));

        validateRequest.ProcessId = processId;
        validateRequest.Address = address;
        validateRequest.Size = (SIZE_T)parsedSize;

        ok = DeviceIoControl(
            device,
            IOCTL_PROC_IOCTL_VALIDATE_READ_REQUEST,
            &validateRequest,
            sizeof(validateRequest),
            &validateResponse,
            sizeof(validateResponse),
            &bytesReturned,
            NULL);

        if (!ok) {
            fwprintf(stderr, L"DeviceIoControl failed: %lu\n", GetLastError());
            CloseHandle(device);
            return 1;
        }

        wprintf(L"pid=%lu address=0x%I64x size=%Iu processFound=%lu sizeAccepted=%lu addressRangeValid=%lu validationStatus=0x%08lx bytes=%lu\n",
            validateResponse.ProcessId,
            validateResponse.Address,
            validateResponse.RequestedSize,
            validateResponse.ProcessFound,
            validateResponse.SizeAccepted,
            validateResponse.AddressRangeValid,
            (unsigned long)validateResponse.ValidationStatus,
            bytesReturned);

        CloseHandle(device);
        return 0;
    }

    ZeroMemory(&request, sizeof(request));
    ZeroMemory(&response, sizeof(response));

    request.ProcessId = processId;

    ok = DeviceIoControl(
        device,
        IOCTL_PROC_IOCTL_SET_PROCESS_ID,
        &request,
        sizeof(request),
        &response,
        sizeof(response),
        &bytesReturned,
        NULL);

    if (!ok) {
        fwprintf(stderr, L"DeviceIoControl failed: %lu\n", GetLastError());
        CloseHandle(device);
        return 1;
    }

    wprintf(L"pid=%lu found=%lu ntstatus=0x%08lx bytes=%lu\n",
        response.ProcessId,
        response.Found,
        (unsigned long)response.Status,
        bytesReturned);

    CloseHandle(device);
    return 0;
}

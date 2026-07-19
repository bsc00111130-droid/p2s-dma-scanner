#include <ntddk.h>
#include "Public.h"

#define DEVICE_NAME L"\\Device\\BufferedPidIoctl"
#define SYMBOLIC_LINK_NAME L"\\DosDevices\\BufferedPidIoctl"

DRIVER_UNLOAD BufferedPidUnload;
DRIVER_DISPATCH BufferedPidCreateClose;
DRIVER_DISPATCH BufferedPidDeviceControl;
DRIVER_DISPATCH BufferedPidUnsupported;

static
VOID
CompleteRequest(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information
    )
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
NTSTATUS
HandleQueryProcess(
    _Inout_ PIRP Irp,
    _In_ ULONG InputBufferLength,
    _In_ ULONG OutputBufferLength,
    _Out_ ULONG_PTR* Information
    )
{
    PVOID systemBuffer;
    PBUFFERED_PID_REQUEST request;
    BUFFERED_PID_REQUEST localRequest;
    BUFFERED_PID_RESPONSE response;
    PEPROCESS process;
    NTSTATUS lookupStatus;

    *Information = 0;

    if (InputBufferLength < sizeof(BUFFERED_PID_REQUEST) ||
        OutputBufferLength < sizeof(BUFFERED_PID_RESPONSE)) {
        return STATUS_BUFFER_TOO_SMALL;
    }

    systemBuffer = Irp->AssociatedIrp.SystemBuffer;
    if (systemBuffer == NULL) {
        return STATUS_INVALID_PARAMETER;
    }

    request = (PBUFFERED_PID_REQUEST)systemBuffer;
    if (request->Size != sizeof(BUFFERED_PID_REQUEST)) {
        return STATUS_INVALID_PARAMETER;
    }

    RtlCopyMemory(&localRequest, request, sizeof(localRequest));
    RtlZeroMemory(&response, sizeof(response));

    response.Size = sizeof(response);
    response.ProcessId = localRequest.ProcessId;

    process = NULL;
    lookupStatus = PsLookupProcessByProcessId(
        (HANDLE)(ULONG_PTR)localRequest.ProcessId,
        &process
        );

    response.LookupStatus = lookupStatus;
    response.ProcessExists = NT_SUCCESS(lookupStatus) ? TRUE : FALSE;

    if (process != NULL) {
        ObDereferenceObject(process);
    }

    RtlCopyMemory(systemBuffer, &response, sizeof(response));
    *Information = sizeof(response);

    return STATUS_SUCCESS;
}

NTSTATUS
BufferedPidUnsupported(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
    )
{
    UNREFERENCED_PARAMETER(DeviceObject);
    CompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    return STATUS_INVALID_DEVICE_REQUEST;
}

NTSTATUS
BufferedPidCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
    )
{
    UNREFERENCED_PARAMETER(DeviceObject);
    CompleteRequest(Irp, STATUS_SUCCESS, 0);
    return STATUS_SUCCESS;
}

NTSTATUS
BufferedPidDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp
    )
{
    PIO_STACK_LOCATION stack;
    ULONG ioControlCode;
    ULONG inputBufferLength;
    ULONG outputBufferLength;
    NTSTATUS status;
    ULONG_PTR information;

    UNREFERENCED_PARAMETER(DeviceObject);

    stack = IoGetCurrentIrpStackLocation(Irp);
    ioControlCode = stack->Parameters.DeviceIoControl.IoControlCode;
    inputBufferLength = stack->Parameters.DeviceIoControl.InputBufferLength;
    outputBufferLength = stack->Parameters.DeviceIoControl.OutputBufferLength;
    information = 0;

    switch (ioControlCode) {
    case IOCTL_BUFFERED_PID_QUERY_PROCESS:
        status = HandleQueryProcess(
            Irp,
            inputBufferLength,
            outputBufferLength,
            &information
            );
        break;

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    CompleteRequest(Irp, status, information);
    return status;
}

VOID
BufferedPidUnload(
    _In_ PDRIVER_OBJECT DriverObject
    )
{
    UNICODE_STRING symbolicLinkName;

    RtlInitUnicodeString(&symbolicLinkName, SYMBOLIC_LINK_NAME);
    IoDeleteSymbolicLink(&symbolicLinkName);

    if (DriverObject->DeviceObject != NULL) {
        IoDeleteDevice(DriverObject->DeviceObject);
    }
}

NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    UNICODE_STRING deviceName;
    UNICODE_STRING symbolicLinkName;
    PDEVICE_OBJECT deviceObject;
    NTSTATUS status;
    ULONG i;

    UNREFERENCED_PARAMETER(RegistryPath);

    RtlInitUnicodeString(&deviceName, DEVICE_NAME);
    deviceObject = NULL;

    status = IoCreateDevice(
        DriverObject,
        0,
        &deviceName,
        FILE_DEVICE_UNKNOWN,
        FILE_DEVICE_SECURE_OPEN,
        FALSE,
        &deviceObject
        );

    if (!NT_SUCCESS(status)) {
        return status;
    }

    RtlInitUnicodeString(&symbolicLinkName, SYMBOLIC_LINK_NAME);
    status = IoCreateSymbolicLink(&symbolicLinkName, &deviceName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(deviceObject);
        return status;
    }

    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; ++i) {
        DriverObject->MajorFunction[i] = BufferedPidUnsupported;
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE] = BufferedPidCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = BufferedPidCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = BufferedPidDeviceControl;
    DriverObject->DriverUnload = BufferedPidUnload;

    deviceObject->Flags |= DO_BUFFERED_IO;
    deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}

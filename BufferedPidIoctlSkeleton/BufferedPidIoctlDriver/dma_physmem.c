/*
 * dma_physmem.c  —  WDM Kernel Driver: FPGA DMA Physical Memory Reader
 * Extends proc_ioctl_driver.c IOCTL skeleton with real DMA via FPGA PCIe BAR
 *
 * Architecture:
 *   User-mode (PID + VA) → Kernel: translate VA→PA via process page tables
 *   → Write DMA descriptor to FPGA BAR2 → FPGA reads PA via PCIe → Data in BAR4
 *
 * Build: VS2022 + WDK, Kernel Mode Driver (WDM), x64, Windows 10/11
 */

#include <ntddk.h>
#include <wdm.h>
#include <intrin.h>

#define DEVICE_NAME         L"\\Device\\DmaPhysMem"
#define SYMBOLIC_NAME       L"\\DosDevices\\DmaPhysMem"
#define DEVICE_TAG          'mADm'

/* ============================================================================
 * Shared IOCTL structures (user ↔ kernel)
 * ============================================================================
 */
typedef struct {
    ULONG   Version;        // 0x00010001
    ULONG   Size;           // sizeof(this)
    ULONG   ProcessId;      // target process ID
    ULONG   Pad0;
    ULONGLONG VirtualAddr;  // target virtual address (in process context)
    ULONG   Size;           // bytes to read (1..4096)
    ULONG   Flags;          // 0=read, 1=write
    ULONGLONG Result;       // output: NTSTATUS
} DmaPhysMemRequest;

typedef struct {
    ULONG   Version;        // 0x00010001
    ULONG   Size;
    ULONG   ProcessId;
    ULONGLONG VirtualAddr;
    ULONG   RequestedSize;
    ULONG   Flags;
    ULONGLONG PhysAddr;     // resolved physical address
    ULONG   BytesTransferred;
    LONG    TranslationStatus;
    LONG    DmaStatus;
    UCHAR   Data[4096];     // read data inline
} DmaPhysMemResponse;

#define IOCTL_DMA_READ   CTL_CODE(FILE_DEVICE_UNKNOWN, 0x820, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)
#define IOCTL_DMA_WRITE  CTL_CODE(FILE_DEVICE_UNKNOWN, 0x821, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)

/* ============================================================================
 * FPGA PCIe BAR mapping (probed at DriverEntry)
 * ============================================================================
 */
typedef struct {
    PVOID   Bar2Va;         // DMA descriptor doorbell
    PVOID   Bar4Va;         // DMA data buffer
    PHYSICAL_ADDRESS Bar2Pa;
    PHYSICAL_ADDRESS Bar4Pa;
    ULONG   Bar2Len;
    ULONG   Bar4Len;
    UINT16  VendorId;
    UINT16  DeviceId;
    UINT8   Bus;
    UINT8   DevFn;
    BOOLEAN Found;
    BOOLEAN MappedBar2;
    BOOLEAN MappedBar4;
} FpgaDevice;

static FpgaDevice g_Fpga = {0};

/* ============================================================================
 * Locate FPGA PCIe device by VID/DID and map BARs
 * ============================================================================
 */
static NTSTATUS
FindAndMapFpga(VOID) {
    NTSTATUS status;
    ULONG bytes;
    PPCI_COMMON_CONFIG pciConfig;
    UCHAR buffer[4096];
    PHYSICAL_ADDRESS pa;
    ULONG barVal;
    ULONG barSize;

    /* Scan PCI bus for VID=10EC, DID=8125 */
    for (ULONG bus = 0; bus < 256; ++bus) {
        for (ULONG dev = 0; dev < 32; ++dev) {
            for (ULONG fn = 0; fn < 8; ++fn) {
                /* Read PCI config space via HAL */
                pciConfig = (PPCI_COMMON_CONFIG)&buffer;
                status = HalGetBusDataByOffset(
                    PCIConfiguration, bus, (dev << 3) | fn,
                    pciConfig, 0, sizeof(PCI_COMMON_CONFIG));
                if (!NT_SUCCESS(status)) continue;

                if (pciConfig->VendorID != 0x10EC) continue;
                if (pciConfig->DeviceID != 0x8125) continue;

                /* Found! */
                g_Fpga.Bus = (UINT8)bus;
                g_Fpga.DevFn = (UINT8)((dev << 3) | fn);
                g_Fpga.VendorId = pciConfig->VendorID;
                g_Fpga.DeviceId = pciConfig->DeviceID;
                g_Fpga.Found = TRUE;

                /* Read BAR0 (offset 0x10) */
                RtlCopyMemory(&barVal, (PUCHAR)pciConfig + 0x10, 4);
                /* BAR2 at offset 0x18 */
                RtlCopyMemory(&barVal, (PUCHAR)pciConfig + 0x18, 4);
                pa.QuadPart = barVal & 0xFFFFFFF0;

                /* Determine BAR2 size by writing all 1s */
                ULONG savedBar = barVal;
                ULONG allOnes = 0xFFFFFFFF;
                HalSetBusDataByOffset(PCIConfiguration, bus, (dev << 3) | fn,
                    &allOnes, 0x18, 4);
                RtlCopyMemory(&barSize, (PUCHAR)pciConfig + 0x18, 4);
                HalSetBusDataByOffset(PCIConfiguration, bus, (dev << 3) | fn,
                    &savedBar, 0x18, 4);
                barSize = barSize & 0xFFFFFFF0;
                barSize = (~barSize) + 1;

                if (pa.QuadPart == 0) continue;

                /* Map BAR2 */
                g_Fpga.Bar2Pa = pa;
                g_Fpga.Bar2Len = barSize;
                g_Fpga.Bar2Va = MmMapIoSpace(pa, barSize, MmNonCached);
                if (!g_Fpga.Bar2Va) continue;
                g_Fpga.MappedBar2 = TRUE;

                /* BAR4 at offset 0x20 */
                RtlCopyMemory(&barVal, (PUCHAR)pciConfig + 0x20, 4);
                pa.QuadPart = barVal & 0xFFFFFFF0;
                /* upper DWORD at 0x24 for 64-bit */
                ULONG upper;
                RtlCopyMemory(&upper, (PUCHAR)pciConfig + 0x24, 4);
                pa.HighPart = upper;

                RtlCopyMemory(&barSize, (PUCHAR)pciConfig + 0x20, 4);
                barSize = barSize & 0xFFFFFFF0;
                barSize = (~barSize) + 1;

                if (pa.QuadPart) {
                    g_Fpga.Bar4Pa = pa;
                    g_Fpga.Bar4Len = barSize < 4096 ? 4096 : barSize;
                    g_Fpga.Bar4Va = MmMapIoSpace(pa, g_Fpga.Bar4Len, MmNonCached);
                    if (g_Fpga.Bar4Va) g_Fpga.MappedBar4 = TRUE;
                }

                return STATUS_SUCCESS;
            }
        }
    }
    return STATUS_DEVICE_NOT_CONNECTED;
}

/* ============================================================================
 * Translate user VA → physical PA via target process page tables
 * Halts on DMA TLB miss (no software walker — hardware does it)
 * ============================================================================
 */
static PHYSICAL_ADDRESS
TranslateVaToPa(HANDLE processId, ULONGLONG va) {
    PHYSICAL_ADDRESS pa = {0};
    PEPROCESS eprocess = NULL;
    KAPC_STATE apc;

    NTSTATUS status = PsLookupProcessByProcessId(processId, &eprocess);
    if (!NT_SUCCESS(status)) return pa;

    /* Attach to target process to access its page tables */
    KeStackAttachProcess(eprocess, &apc);

    /* Use MmGetPhysicalAddress (kernel API, works for user pages too) */
    __try {
        pa = MmGetPhysicalAddress((PVOID)(ULONG_PTR)va);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        pa.QuadPart = 0;
    }

    KeUnstackDetachProcess(&apc);
    ObDereferenceObject(eprocess);
    return pa;
}

/* ============================================================================
 * DMA read via FPGA — Write descriptor to BAR2, poll completion, read BAR4
 * ============================================================================
 */
static NTSTATUS
DmaReadPhysical(PHYSICAL_ADDRESS targetPa, PUCHAR buffer, ULONG size) {
    volatile ULONG64* doorbell;
    volatile ULONG64* descAddr;
    volatile ULONG64* descSize;
    volatile ULONG64* statusReg;
    ULONG64 timeout;
    ULONG i;

    if (!g_Fpga.MappedBar2 || !g_Fpga.Bar2Va) return STATUS_DEVICE_NOT_READY;

    /* FPGA DMA descriptor layout at BAR2+0x00:
     *   +0x00: target physical address (64-bit)
     *   +0x08: size (32-bit) + flags (32-bit)
     *   +0x10: doorbell (write 1 to trigger)
     *   +0x18: status (0=idle, 1=busy, 2=done, 0xFFFFFFFF=error)
     * Data returned at BAR4+0x00 (up to 4096 bytes)
     */
    descAddr = (volatile ULONG64*)((PUCHAR)g_Fpga.Bar2Va + 0x00);
    descSize = (volatile ULONG64*)((PUCHAR)g_Fpga.Bar2Va + 0x08);
    doorbell = (volatile ULONG64*)((PUCHAR)g_Fpga.Bar2Va + 0x10);
    statusReg = (volatile ULONG64*)((PUCHAR)g_Fpga.Bar2Va + 0x18);

    /* Write descriptor */
    WRITE_REGISTER_ULONG64((ULONG64*)descAddr, targetPa.QuadPart);
    WRITE_REGISTER_ULONG64((ULONG64*)descSize, (ULONG64)size);
    _mm_sfence();

    /* Ring doorbell */
    WRITE_REGISTER_ULONG64((ULONG64*)doorbell, 1);
    _mm_sfence();

    /* Poll completion (timeout ~10ms at 100MHz = 1M cycles) */
    timeout = 0;
    while (timeout < 1000000) {
        ULONG64 st = READ_REGISTER_ULONG64((ULONG64*)statusReg);
        if (st == 2) break;         // done
        if (st == 0xFFFFFFFF) {     // error
            _mm_pause();
            return STATUS_UNSUCCESSFUL;
        }
        _mm_pause();
        timeout++;
    }

    if (timeout >= 1000000) return STATUS_IO_TIMEOUT;

    /* Copy data from BAR4 */
    if (g_Fpga.MappedBar4 && g_Fpga.Bar4Va) {
        ULONG readSize = size > 4096 ? 4096 : size;
        for (i = 0; i < readSize; i += 8) {
            ULONG64 val = READ_REGISTER_ULONG64(
                (ULONG64*)((PUCHAR)g_Fpga.Bar4Va + i));
            __builtin_memcpy(buffer + i, &val, (readSize - i < 8) ? (readSize - i) : 8);
        }
    }

    return STATUS_SUCCESS;
}

/* ============================================================================
 * IOCTL handler: DMA read with VA→PA translation
 * ============================================================================
 */
static NTSTATUS
HandleDmaRead(PIRP Irp, PIO_STACK_LOCATION IrpSp) {
    DmaPhysMemRequest* req;
    DmaPhysMemResponse* resp;
    PHYSICAL_ADDRESS pa;
    NTSTATUS status;
    ULONG size;

    req = (DmaPhysMemRequest*)Irp->AssociatedIrp.SystemBuffer;
    resp = (DmaPhysMemResponse*)Irp->AssociatedIrp.SystemBuffer;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(DmaPhysMemRequest) ||
        IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(DmaPhysMemResponse)) {
        Irp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
        Irp->IoStatus.Information = 0;
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlZeroMemory(resp, sizeof(*resp));
    resp->Version = req->Version;
    resp->ProcessId = req->ProcessId;
    resp->VirtualAddr = req->VirtualAddr;
    resp->RequestedSize = req->Size;
    resp->Flags = req->Flags;

    if (req->ProcessId == 0 || req->VirtualAddr == 0 || req->Size == 0 || req->Size > 4096) {
        resp->TranslationStatus = STATUS_INVALID_PARAMETER;
        Irp->IoStatus.Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = sizeof(*resp);
        return STATUS_SUCCESS;
    }

    /* Step 1: Translate VA → PA via target process page tables */
    pa = TranslateVaToPa(UlongToHandle(req->ProcessId), req->VirtualAddr);
    resp->PhysAddr = pa.QuadPart;
    if (pa.QuadPart == 0) {
        resp->TranslationStatus = STATUS_PROCESS_IS_TERMINATING;
        Irp->IoStatus.Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = sizeof(*resp);
        return STATUS_SUCCESS;
    }
    resp->TranslationStatus = STATUS_SUCCESS;

    /* Step 2: DMA read from physical address via FPGA */
    size = req->Size;
    status = DmaReadPhysical(pa, resp->Data, size);
    resp->DmaStatus = status;

    if (NT_SUCCESS(status)) {
        resp->BytesTransferred = size;
    }

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = sizeof(*resp) - 4096 + size;
    return STATUS_SUCCESS;
}

/* ============================================================================
 * Driver entry
 * ============================================================================
 */
NTSTATUS
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath) {
    UNREFERENCED_PARAMETER(RegistryPath);
    NTSTATUS status;
    PDEVICE_OBJECT deviceObject = NULL;
    UNICODE_STRING devName, symName;

    RtlInitUnicodeString(&devName, DEVICE_NAME);
    status = IoCreateDevice(DriverObject, 0, &devName,
                            FILE_DEVICE_UNKNOWN, 0, FALSE, &deviceObject);
    if (!NT_SUCCESS(status)) return status;

    RtlInitUnicodeString(&symName, SYMBOLIC_NAME);
    status = IoCreateSymbolicLink(&symName, &devName);
    if (!NT_SUCCESS(status)) {
        IoDeleteDevice(deviceObject);
        return status;
    }

    /* Find and map FPGA */
    status = FindAndMapFpga();

    DriverObject->MajorFunction[IRP_MJ_CREATE] =
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = [](PDEVICE_OBJECT, PIRP Irp) {
        Irp->IoStatus.Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    };

    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] =
        [](PDEVICE_OBJECT, PIRP Irp) {
        auto irpSp = IoGetCurrentIrpStackLocation(Irp);
        NTSTATUS st;

        switch (irpSp->Parameters.DeviceIoControl.IoControlCode) {
        case IOCTL_DMA_READ:
            st = HandleDmaRead(Irp, irpSp);
            break;
        default:
            st = STATUS_INVALID_DEVICE_REQUEST;
            Irp->IoStatus.Status = st;
            Irp->IoStatus.Information = 0;
            break;
        }
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return st;
    };

    DriverObject->DriverUnload = [](PDRIVER_OBJECT) {
        if (g_Fpga.MappedBar4 && g_Fpga.Bar4Va)
            MmUnmapIoSpace(g_Fpga.Bar4Va, g_Fpga.Bar4Len);
        if (g_Fpga.MappedBar2 && g_Fpga.Bar2Va)
            MmUnmapIoSpace(g_Fpga.Bar2Va, g_Fpga.Bar2Len);
        IoDeleteSymbolicLink(&(UNICODE_STRING)RTL_CONSTANT_SYMBOLIC_NAME(SYMBOLIC_NAME));
        IoDeleteDevice(NULL);
    };

    deviceObject->Flags |= DO_BUFFERED_IO;
    deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

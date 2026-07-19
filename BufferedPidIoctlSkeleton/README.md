# Buffered PID IOCTL Skeleton

This is a minimal Windows 11 WDM driver skeleton that demonstrates a standard
`METHOD_BUFFERED` IOCTL path. The user-mode app sends a process ID through a
shared request structure. The driver maps `Irp->AssociatedIrp.SystemBuffer` to
that request, validates the size fields, looks up the process object, returns a
small response, and immediately dereferences the process object.

The sample does not read or write process memory and does not alter the target
process.

## Layout

```text
BufferedPidIoctlDriver/
  Driver.c
  Public.h
  BufferedPidIoctlDriver.inf
  BufferedPidIoctlDriver.vcxproj
UserModeTest/
  main.c
  UserModeTest.vcxproj
BufferedPidIoctl.sln
```

## Key IOCTL Flow

1. User mode fills `BUFFERED_PID_REQUEST`.
2. `DeviceIoControl` uses `IOCTL_BUFFERED_PID_QUERY_PROCESS`.
3. Kernel mode reads the request from `Irp->AssociatedIrp.SystemBuffer`.
4. Kernel mode checks `InputBufferLength`, `OutputBufferLength`, and `Size`.
5. Kernel mode calls `PsLookupProcessByProcessId`.
6. Kernel mode copies `BUFFERED_PID_RESPONSE` back into `SystemBuffer`.

## Build Notes

Use Visual Studio with the Windows Driver Kit installed.

Open:

```text
BufferedPidIoctl.sln
```

Build the `x64` configuration. Loading kernel drivers on Windows 11 requires
administrator rights and a properly signed driver package for the target
machine's driver-signing policy.

## User-Mode Test

After the driver is installed and running:

```powershell
.\UserModeTest.exe <pid>
```

Expected successful output includes:

```text
bytesReturned=32
response.ProcessId=<pid>
response.LookupStatus=0x00000000
response.ProcessExists=true
```

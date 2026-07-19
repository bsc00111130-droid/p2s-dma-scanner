# METHOD_BUFFERED IOCTL Driver Skeleton

This folder contains a minimal Windows 10 WDM-style IOCTL skeleton.

## Files

- `include/proc_ioctl_shared.h` - IOCTL code and user/kernel request structures.
- `driver/proc_ioctl_driver.c` - kernel driver skeleton using `METHOD_BUFFERED`.
- `user/proc_ioctl_client.c` - user-mode sample that sends a PID or a validation request with `DeviceIoControl`.
- `user/proc_ioctl_controller.cpp` - user-mode two-thread controller sample using QPC timing, a high-resolution waitable timer, and a lock-free SPSC queue.
- `gui/index.html` - standalone local dashboard for contract, queue, packet, and safety diagnostics.
- `gui/index.html` - local safe diagnostics GUI for validation, deterministic payloads, packet framing, queue telemetry, and optional Web Serial output.

## Driver Behavior

The driver creates:

- Kernel device: `\Device\ProcIoctlDemo`
- Win32 path: `\\.\ProcIoctlDemo`

The sample IOCTL receives:

```c
typedef struct _PROC_IOCTL_PID_REQUEST {
    unsigned long ProcessId;
} PROC_IOCTL_PID_REQUEST;
```

Because the IOCTL uses `METHOD_BUFFERED`, the input and output share
`Irp->AssociatedIrp.SystemBuffer`. The driver validates input/output sizes,
copies the PID into a response, and calls `PsLookupProcessByProcessId` only to
check whether the target process exists. It does not read or write target
process memory.

The validation IOCTL receives:

```c
typedef struct _PROC_IOCTL_VALIDATE_READ_REQUEST {
    unsigned long ProcessId;
    unsigned long long Address;
    SIZE_T Size;
} PROC_IOCTL_VALIDATE_READ_REQUEST;
```

It performs only contract validation:

- rejects zero PID, zero address, and zero size,
- rejects requests where `Size >= 0x1000`,
- rejects arithmetic overflow in `Address + Size - 1`,
- rejects ranges above `MmUserProbeAddress`,
- checks whether the PID currently resolves to an `EPROCESS`.

It never calls `MmCopyVirtualMemory`, never attaches to the target process, and
never reads target memory. `AddressRangeValid` means the requested range has a
valid user-mode address shape; it does not prove the remote pages are mapped or
readable.

The test-data IOCTL uses the same request contract and returns a validation
header plus deterministic kernel-owned payload bytes. It is intended for
buffering, timing, and serial loopback tests. It does not copy from any target
process address; the address field only participates in validation and in the
deterministic test pattern seed.

The shared-buffer IOCTL maps a driver-owned nonpaged test buffer into the
calling controller process with an MDL. A kernel timer updates that buffer with
deterministic payload bytes. This is for shared-memory latency testing only; it
does not map or expose memory from any target process.

The caller-buffer lock IOCTL demonstrates the safe MDL page-fault and page-lock
path on the caller's own user-mode buffer only. It caps the request below 4KB,
calls `MmProbeAndLockPages(..., UserMode, IoReadAccess)`, reports the number of
pages touched, and immediately unlocks the pages before completing the IOCTL.
It does not accept a PID and does not lock pages belonging to another process.

The old `user/proc_ioctl_readmem.c` tool name is retained as a disabled
compatibility placeholder. `PROC_IOCTL_ENABLE_UNSAFE_READMEM` is fixed at `0`,
there is no active `READ_PROCESS_MEMORY` dispatch path, and the driver does not
call `MmCopyVirtualMemory`. See `docs/UNSAFE_READMEM_DISABLED.md`.

## Build Notes

Use the Windows Driver Kit matching your Visual Studio installation.

Typical driver project setup:

1. Create an empty **Kernel Mode Driver, Empty (WDM)** project.
2. Add `driver/proc_ioctl_driver.c`.
3. Build for x64 Windows 10.
4. Test on a VM or test-signing machine.

Typical user-mode build from a Developer Command Prompt:

```cmd
cl /W4 /nologo user\proc_ioctl_client.c
cl /std:c++17 /EHsc /W4 /nologo user\proc_ioctl_controller.cpp
cl /std:c++17 /EHsc /W4 /nologo user\kalman_motion_demo.cpp
```

After installing Visual Studio Build Tools and the WDK, the included scripts can
build the current project from a normal PowerShell window:

```cmd
npm run build:user
npm run build:driver
npm run check
```

Build outputs are written under `build\`.

Example calls:

```cmd
proc_ioctl_client.exe 1234
proc_ioctl_client.exe 1234 0x7ff700001000 128
proc_ioctl_client.exe lock-self 512
proc_ioctl_controller.exe 1234 0x7ff700001000 128 COM3 1000
proc_ioctl_controller.exe 1234 0x7ff700001000 128 - 500
proc_ioctl_controller.exe 1234 0x7ff700001000 128 COM3 1000 shared
kalman_motion_demo.exe
kalman_motion_demo.exe --self-test
```

Local GUI:

```cmd
python gui\serve_gui.py
```

Open `http://127.0.0.1:8765/`. The GUI is a safe diagnostics surface; it does
not call the kernel driver or read target process memory.

`user/kalman_motion_filter.hpp` is a standalone C++17 utility for smoothing
noisy 1D/2D coordinate streams and producing a distance-scaled, acceleration
limited motion profile. It does not read process memory or send hardware input.
The higher-level `MotionPacketPipeline` combines:

1. raw 2D coordinate input,
2. optional outlier gating,
3. constant-velocity Kalman filtering,
4. distance-scaled acceleration-limited motion,
5. fractional residual accumulation,
6. fixed 8-byte MCU packet encoding,
7. packet decode/checksum validation for diagnostics.

The same header also contains a fixed 8-byte MCU move packetizer:

```text
byte 0..1: header, little-endian, default 0xA55A
byte 2   : command, default 0x01
byte 3..4: signed X delta, little-endian int16
byte 5..6: signed Y delta, little-endian int16
byte 7   : sum8 checksum over bytes 0..6
```

Use `PacketizeMoveDelta()` when converting floating-point per-frame motion. It
keeps fractional residuals so sub-pixel movement is preserved across frames.
`McuPacketizerConfig` supports `sum8`, `xor8`, and two's-complement sum8
checksums, axis inversion, and `CountsPerUnit` scaling when the MCU expects
device counts rather than screen or world units. Use `DecodeMovePacket()` or
`kalman_motion_demo.exe --self-test` to validate byte order and checksums before
connecting a device.

The controller sample has two user-mode threads:

- reader thread: periodically calls `IOCTL_PROC_IOCTL_GET_TEST_DATA`,
- sender thread: submits a fixed binary packet header plus payload as one
  overlapped `WriteFile` frame to the serial port, or drains samples in dry-run
  mode when the port argument is `-`.

With the optional `shared` argument, startup maps the driver-owned shared test
buffer with `IOCTL_PROC_IOCTL_MAP_TEST_BUFFER`, the reader thread reads that
mapped pointer instead of issuing per-sample IOCTLs, and shutdown sends
`IOCTL_PROC_IOCTL_UNMAP_TEST_BUFFER`.

It uses no mutexes or critical sections. The thread handoff is a single-producer,
single-consumer ring buffer implemented with `std::atomic` acquire/release
operations. It does not use `Sleep(1)`. Timing uses QPC deadlines, a requested
0.5ms NT timer resolution via `NtSetTimerResolution`, a high-resolution waitable
timer when available, and a short final spin wait.

The serial path opens the COM device with `FILE_FLAG_OVERLAPPED`, requests a
small 64-byte input queue and 8192-byte output queue with `SetupComm`, disables
software and hardware flow-control waits in the `DCB`, sets immediate-return
read timeouts plus zero write timeout constants, and purges stale RX/TX queues.
The sender owns eight independent `OVERLAPPED` write slots, checks completion
with `HasOverlappedIoCompleted`, and submits the next frame without waiting for
hardware completion. If all async write slots are still busy, it drops that
sample and increments `serialBusyDropped`.

Sub-millisecond periodic scheduling is still not a hard real-time guarantee on
Windows user mode. The sample is a low-latency architecture, not a deterministic
real-time contract.

Driver loading requires administrator rights and a properly signed or test-signed
driver package.

## GUI

The `gui` folder contains a static local dashboard. Open
`gui/launch_gui.cmd` to start the local dashboard, or open `gui/index.html`
directly in a browser. It is intentionally separate from the driver and
controller binaries, so it adds a polished diagnostic surface without changing
the kernel ABI or the existing C/C++ build flow.

Run the safety audit at any time:

```cmd
powershell -NoProfile -ExecutionPolicy Bypass -File tools\verify_safe_surface.ps1
```

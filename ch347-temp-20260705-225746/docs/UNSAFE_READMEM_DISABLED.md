# Unsafe Read-Memory Surface Is Disabled

This project keeps the old read-memory tool name as a compatibility placeholder,
but arbitrary target-process memory reads are not implemented or dispatched.

Active data paths are limited to:

- PID and request-shape validation.
- Deterministic kernel-owned test payloads.
- Driver-owned shared test buffer mapping.
- Caller-owned buffer lock diagnostics below 4 KB.

The project-wide marker is:

```c
#define PROC_IOCTL_ENABLE_UNSAFE_READMEM 0
```

Changing that marker is intentionally not enough to enable process memory reads.
The driver contains no active `READ_PROCESS_MEMORY` dispatch case and does not
call `MmCopyVirtualMemory`.

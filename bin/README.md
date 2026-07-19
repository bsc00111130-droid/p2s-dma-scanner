# Binary Samples for Static Analysis

These executables are provided for educational static analysis study:
- Examine PE headers, import tables, section layouts
- Study MSVC linker output patterns
- Analyze manifest embedding (requireAdministrator)
- Practice with tools like pestudio, PE-bear, Detect It Easy

## Files

- **msw_inject.exe** (141KB) ? CreateRemoteThread-based DLL injector with SeDebugPrivilege
- **apc_inject.exe** (141KB) ? APC (QueueUserAPC) injection variant
- **keep_inject.exe** (141KB) ? Looping injector that waits for target process
- **p2s_gui.exe** (186KB) ? Win32 GUI memory scanner (user-mode IOCTL client)

These are NOT the original third-party tool; they are original binaries compiled
from the source code in this repository for educational analysis.

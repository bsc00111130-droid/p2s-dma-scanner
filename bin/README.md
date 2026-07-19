# Binary Samples for Static Analysis

These executables are provided for educational static analysis study:
- Examine PE headers, import tables, section layouts
- Study PyInstaller bundle structure (CArchive, PYZ, PKG)
- Analyze manifest embedding (requireAdministrator vs asInvoker)
- Practice with tools like pestudio, PE-bear, Detect It Easy, pyinstxtractor

## Files

| File | Size | Analysis Focus |
|------|------|----------------|
| **MSWloader_personal.exe** | 17.3MB | PyInstaller 6.x bundle - study CArchive/PYZ archive structure, python312.dll embedding |
| **msw_inject.exe** | 141KB | CreateRemoteThread-based DLL injector - IAT, admin manifest |
| **apc_inject.exe** | 141KB | QueueUserAPC injection variant - thread enumeration, TH32CS_SNAPSNAPTHREAD |
| **keep_inject.exe** | 141KB | Looping injector - WaitForSingleObject timeout patterns |
| **p2s_gui.exe** | 186KB | Win32 GUI - GDI object layout, Common Controls, IOCTL dispatch |

## Static Analysis Commands

```powershell
# PE header analysis
dumpbin /headers msw_inject.exe

# Import Address Table
dumpbin /imports msw_inject.exe

# Section layout
dumpbin /sections apc_inject.exe

# PyInstaller bundle structure (MSWloader_personal.exe)
pyi-archive_viewer MSWloader_personal.exe
pyinstxtractor MSWloader_personal.exe

# Manifest extraction
mt.exe -inputresource:msw_inject.exe -out:manifest.xml
```

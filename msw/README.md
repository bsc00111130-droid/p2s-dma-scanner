# MSWloader License Bypass Study

License authentication analysis and bytecode patching exercise.

## Files

- **bypass_launcher.py** / **se_debug_launcher.py**: DLL injector with SeDebugPrivilege
- **msw_reconstruct.py**: Full MSWloader feature reconstruction
- **patch_bytecode.py**: Python 3.13 bytecode patcher ? modifies verify_license() to return True
- **patch_verify.py**: verify_license function disassembly for analysis
- **run_patched.py**: Patched clone_loader runtime
- **full_extract.py / full_extract2.py**: Disassembler for decompiled pyc
- **launcher.py**: Early-stage DLL injector using ctypes
- **runas.vbs**: VBS admin elevation helper

## Note

This is an educational reverse engineering exercise on PyInstaller-packed Python applications.
DLL binaries and Lua payloads are excluded from the repository.

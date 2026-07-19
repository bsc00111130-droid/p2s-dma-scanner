# -*- coding: utf-8 -*-
"""
MSWloader License Bypass Loader
Monkey-patches verify_license at runtime before calling main()
"""
import sys, os, marshal, types

# 1. Replace original clone_loader.pyc with patched version
EXT = r"C:\Users\zAmA\Desktop\새 폴더\MSWloader_1023.exe_extracted"
PATCHED = os.path.join(EXT, "clone_loader_patched.pyc")
ORIG = os.path.join(EXT, "clone_loader.pyc")

# Backup original if not already backed up
BAK = os.path.join(EXT, "clone_loader_original.pyc")
if not os.path.exists(BAK) and os.path.exists(ORIG):
    os.rename(ORIG, BAK)
    print(f"[*] Backed up original to clone_loader_original.pyc")

# Copy patched version as active
if os.path.exists(PATCHED):
    import shutil
    shutil.copy2(PATCHED, ORIG)
    print(f"[+] Patched clone_loader.pyc installed")

# 2. Add extracted dir to sys.path so imports find our files
sys.path.insert(0, EXT)
os.chdir(EXT)

# 3. Run the loader
print("[*] Loading clone_loader...")
try:
    import clone_loader
    clone_loader.main()
except Exception as e:
    print(f"[!] Error: {e}")
    import traceback
    traceback.print_exc()

import marshal, dis, types, sys

ORIG = r"C:\Users\zAmA\Desktop\새 폴더\MSWloader_1023.exe_extracted\clone_loader.pyc"
OUT = r"C:\Users\zAmA\Desktop\새 폴더\MSWloader_1023.exe_extracted\clone_loader_patched.pyc"

# Read original
with open(ORIG, "rb") as f:
    header = f.read(16)
    top_code = marshal.load(f)

print(f"Python {header[2]}.{header[3]} - magic {int.from_bytes(header[:4],'little'):x}")

# Find verify_license in top_code.co_consts
patched = False
new_consts = list(top_code.co_consts)

for i, const in enumerate(new_consts):
    if hasattr(const, "co_code") and const.co_name == "verify_license":
        print(f"Found verify_license at const[{i}]")
        
        # Build replacement: return ({success:True,...}, True)
        # Python 3.13 bytecode instructions:
        bytecode = bytes([
            0x97, 0x00,           # RESUME 0
            0x64, 0x01,           # LOAD_CONST 1  (True)
            0x64, 0x03,           # LOAD_CONST 3  (fake_response dict)
            0x66, 0x02,           # BUILD_TUPLE 2
            0x53, 0x00,           # RETURN_VALUE
        ])
        
        new_consts_list = list(const.co_consts)
        # Add fake response dict as const[3]
        fake = {
            "success": True,
            "info": {"subscriptions": [{"expiry": "9999999999", "timeleft": 999999}]},
            "message": "license valid"
        }
        if len(new_consts_list) <= 3:
            new_consts_list.append(fake)
        else:
            new_consts_list[3] = fake
        
        # Build new code object
        new_code = types.CodeType(
            const.co_argcount, const.co_posonlyargcount,
            const.co_kwonlyargcount, const.co_nlocals,
            2,  # stacksize reduced
            const.co_flags, bytecode,
            tuple(new_consts_list), const.co_names,
            const.co_varnames, const.co_filename,
            const.co_name, const.co_qualname,
            const.co_firstlineno, const.co_lnotab,
            const.co_exceptiontable, tuple(const.co_freevars),
            tuple(const.co_cellvars)
        )
        new_consts[i] = new_code
        patched = True
        print(f"Patched verify_license: {len(bytecode)} bytes")
        break

if not patched:
    print("[!] verify_license not found")
    sys.exit(1)

# Rebuild top-level code with updated constants
new_top = types.CodeType(
    top_code.co_argcount, top_code.co_posonlyargcount,
    top_code.co_kwonlyargcount, top_code.co_nlocals,
    top_code.co_stacksize, top_code.co_flags, top_code.co_code,
    tuple(new_consts), top_code.co_names, top_code.co_varnames,
    top_code.co_filename, top_code.co_name, top_code.co_qualname,
    top_code.co_firstlineno, top_code.co_lnotab,
    top_code.co_exceptiontable, tuple(top_code.co_freevars),
    tuple(top_code.co_cellvars)
)

# Write patched .pyc
with open(OUT, "wb") as f:
    f.write(header)
    marshal.dump(new_top, f)

print(f"\n[+] Patched file: {OUT}")
print("    Replace original clone_loader.pyc with this file")
print("    Then repackage PYZ.pyz and rebuild EXE with PyInstaller")

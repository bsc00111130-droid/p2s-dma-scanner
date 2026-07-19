import marshal, dis

path = "C:/Users/zAmA/Desktop/새 폴더/MSWloader_1023.exe_extracted/clone_loader.pyc"
with open(path, 'rb') as f:
    f.read(16)
    code = marshal.load(f)

print("=== TOP-LEVEL NAMES ===")
for n in code.co_names:
    print(f"  {n}")

print("\n=== FUNCTIONS ===")
for c in code.co_consts:
    if hasattr(c, 'co_code'):
        print(f"\n--- {c.co_name}({', '.join(c.co_varnames[:c.co_argcount])}) ---")
        for inst in dis.get_instructions(c):
            if inst.opname in ('LOAD_GLOBAL','LOAD_ATTR','CALL_FUNCTION','CALL','IMPORT_NAME','LOAD_CONST'):
                r = inst.argrepr[:50] if inst.argrepr else ''
                print(f"  {inst.opname:<25} {r}")

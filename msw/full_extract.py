import marshal, dis

path = "C:/Users/zAmA/Desktop/새 폴더/MSWloader_1023.exe_extracted/clone_loader.pyc"
with open(path, 'rb') as f:
    f.read(16)
    code = marshal.load(f)

# Print full bytecode for main function  
for c in code.co_consts:
    if hasattr(c, 'co_code') and c.co_name == 'main':
        print(f"=== MAIN FUNCTION: {c.co_name} ===")
        print(f"Args: {c.co_varnames[:c.co_argcount]}")
        print(f"Vars: {c.co_varnames}")
        print(f"Consts: {[x for x in c.co_consts if isinstance(x, str)]}")
        print()
        for inst in dis.get_instructions(c):
            print(f"  {inst.startsource or '':>4} {inst.opname:<25} {inst.argrepr}")
        break

print("\n\n=== ALL TOP-LEVEL CONSTS ===")
for c in code.co_consts:
    if isinstance(c, str):
        print(f"  STR: {c[:100]}")

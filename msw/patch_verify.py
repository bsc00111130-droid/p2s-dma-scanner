import marshal, dis

path = r"C:\Users\zAmA\Desktop\새 폴더\MSWloader_1023.exe_extracted\clone_loader.pyc"
with open(path, "rb") as f:
    header = f.read(16)
    code = marshal.load(f)

magic = int.from_bytes(header[0:4], "little")
print(f"Python version: {header[2]:d}.{header[3]:d}")
print(f"Bytecode magic: {magic:x} ({magic})")

for const in code.co_consts:
    if hasattr(const, "co_code") and const.co_name == "verify_license":
        print(f"\nOriginal verify_license ({len(const.co_code)} bytes):")
        for inst in dis.get_instructions(const):
            print(f"  {inst.offset:4d} {inst.opname:<25s} {inst.argrepr}")
        print(f"\nRaw bytecode ({len(const.co_code)} bytes):")
        print(const.co_code.hex())
        break

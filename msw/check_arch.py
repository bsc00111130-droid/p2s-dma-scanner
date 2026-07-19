import ctypes, struct
k32 = ctypes.windll.kernel32
pid = 22912

h = k32.OpenProcess(0x1F0FFF, False, pid)
if h:
    is_wow = ctypes.c_bool(0)
    if k32.IsWow64Process(h, ctypes.byref(is_wow)):
        print(f"Target: {'32-bit' if is_wow.value else '64-bit'}")
    print(f"Python: {struct.calcsize('P')*8}-bit")
    
    km = k32.GetModuleHandleW("kernel32.dll")
    ll = k32.GetProcAddress(km, b"LoadLibraryW")
    print(f"Our kernel32: 0x{km:x}")
    print(f"Our LoadLibraryW: 0x{ll:x}")
    
    if is_wow.value:
        # Load 32-bit kernel32 to get the right LoadLibraryW addr
        sw = k32.LoadLibraryExW(r"C:\Windows\SysWOW64\kernel32.dll", 0, 1)
        if sw:
            ll32 = k32.GetProcAddress(sw, b"LoadLibraryW")
            print(f"32-bit LoadLibraryW: 0x{ll32:x}")
            k32.FreeLibrary(sw)
    
    k32.CloseHandle(h)

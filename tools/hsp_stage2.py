import ctypes
import ctypes.wintypes as wt
import subprocess
import time
import os
import struct
import sys

GAME_DIR = r"C:\Users\runneradmin\AppData\Local\Temp\2\echidna_extract\Echidna Wars DX [ENG-JAP]"
EXE_PATH = os.path.join(GAME_DIR, "EchidnaWarsDX.exe")
OUT_DIR = r"C:\Users\RUNNER~1\AppData\Local\Temp\2\opencode"

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)
psapi.EnumProcessModules.argtypes = [wt.HANDLE, ctypes.POINTER(wt.HMODULE), wt.DWORD, ctypes.POINTER(wt.DWORD)]
psapi.EnumProcessModules.restype = wt.BOOL
psapi.GetModuleFileNameExW.argtypes = [wt.HANDLE, wt.HMODULE, wt.LPWSTR, wt.DWORD]
psapi.GetModuleFileNameExW.restype = wt.DWORD
psapi.GetModuleInformation.argtypes = [wt.HANDLE, wt.HMODULE, wt.LPVOID, wt.DWORD]
psapi.GetModuleInformation.restype = wt.BOOL

PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010

def read_mem(handle, addr, size):
    buf = ctypes.create_string_buffer(size)
    br = ctypes.c_size_t(0)
    ok = kernel32.ReadProcessMemory(handle, ctypes.c_void_p(addr), buf, size, ctypes.byref(br))
    if not ok:
        return None
    return buf.raw[:br.value]

def enum_modules(handle):
    # EnumProcessModules
    needed = wt.DWORD(0)
    mods = (wt.HMODULE * 1024)()
    ok = psapi.EnumProcessModules(handle, mods, ctypes.sizeof(mods), ctypes.byref(needed))
    if not ok:
        print(f"EnumProcessModules failed {ctypes.get_last_error()}", flush=True)
        return []
    count = needed.value // ctypes.sizeof(wt.HMODULE)
    result = []
    for i in range(count):
        hmod = mods[i]
        name_buf = ctypes.create_unicode_buffer(512)
        psapi.GetModuleFileNameExW(handle, hmod, name_buf, 512)
        mi = wt.DWORD(0)
        # MODULEINFO
        class MODULEINFO(ctypes.Structure):
            _fields_ = [("lpBaseOfDll", wt.LPVOID), ("SizeOfImage", wt.DWORD), ("EntryPoint", wt.LPVOID)]
        minfo = MODULEINFO()
        psapi.GetModuleInformation(handle, hmod, ctypes.byref(minfo), ctypes.sizeof(minfo))
        base = ctypes.cast(minfo.lpBaseOfDll, ctypes.c_void_p).value or 0
        result.append((name_buf.value, base, minfo.SizeOfImage))
    return result

def hexdump(data, base_addr, limit=512):
    n = min(len(data), limit)
    for off in range(0, n, 16):
        chunk = data[off:off+16]
        hexs = " ".join(f"{b:02X}" for b in chunk)
        asc = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        print(f"  0x{base_addr+off:08X}  {hexs:<48}  {asc}", flush=True)

def main():
    proc = subprocess.Popen([EXE_PATH], cwd=GAME_DIR)
    print(f"pid={proc.pid}", flush=True)
    time.sleep(6)
    if proc.poll() is not None:
        print(f"exited early rc={proc.poll()}", flush=True)
        return
    h = kernel32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, proc.pid)
    print(f"handle={h}", flush=True)
    try:
        mods = enum_modules(h)
        print(f"=== {len(mods)} modules ===", flush=True)
        for name, base, size in mods:
            short = os.path.basename(name)
            print(f"  0x{base:08X} +0x{size:X} {short}", flush=True)
            if "76E5" in f"{base:08X}":
                print(f"    ^-- contains earlier DPM2 hit region?", flush=True)
        # Identify which module contains 0x76E5D533 and 0x02505020
        for target in (0x76E5D533, 0x02505020):
            owner = "<unknown>"
            for name, base, size in mods:
                if base <= target < base + size:
                    owner = f"{os.path.basename(name)} base=0x{base:08X}"
                    break
            print(f"0x{target:08X} owned by: {owner}", flush=True)
        # Dump around 0x02505020
        print("=== dump around 0x02505020 (HSP3 peek) ===", flush=True)
        data = read_mem(h, 0x02505020 - 64, 512)
        if data:
            hexdump(data, 0x02505020 - 64)
            # try interpret as HSPHED anyway
            try:
                fields = struct.unpack_from("<4s7I", data, 64)
                print(f"  magic={fields[0]} rest={[hex(x) for x in fields[1:]]}", flush=True)
            except Exception as e:
                print(f"  unpack err {e}", flush=True)
        else:
            print("  read failed", flush=True)
        # Dump around DPM2 hit
        print("=== dump around 0x76E5D533 (DPM2 peek) ===", flush=True)
        data2 = read_mem(h, 0x76E5D533 - 64, 512)
        if data2:
            hexdump(data2, 0x76E5D533 - 64)
        else:
            print("  read failed", flush=True)
        # Search game EXE image + hmm.dll image for patterns
        print("=== pattern search inside game modules ===", flush=True)
        patterns = [b"start.ax", b"start.axe", b"HSP3", b"HSP2", b"DPM2", b"HSPHED", b"HGIMG",
                    b"Hsp3", b"hsp3", b".ax\x00", b"data.dpm", b"packfile"]
        for name, base, size in mods:
            short = os.path.basename(name).lower()
            if short in ("echidnawarsdx.exe", "hmm.dll", "hspda.dll", "hspogg.dll", "ovplay.dll"):
                print(f"-- scanning {short} 0x{base:08X}+0x{size:X} --", flush=True)
                # read in 1MB chunks
                off = 0
                while off < size:
                    chunk_sz = min(1024*1024, size - off)
                    d = read_mem(h, base + off, chunk_sz)
                    if not d:
                        print(f"   read fail at +0x{off:X}", flush=True)
                        break
                    for pat in patterns:
                        s = 0
                        while True:
                            idx = d.find(pat, s)
                            if idx < 0:
                                break
                            print(f"   {pat} at 0x{base+off+idx:08X}", flush=True)
                            s = idx + 1
                    # wide-char search for start.ax
                    s = 0
                    wpat = "start.ax".encode("utf-16-le")
                    while True:
                        idx = d.find(wpat, s)
                        if idx < 0:
                            break
                        print(f"   start.ax(W) at 0x{base+off+idx:08X}", flush=True)
                        s = idx + 1
                    off += chunk_sz
        # Heap spray check: look for high-entropy large RW regions (possible decrypted AX)
        print("=== large RW regions (possible decrypted blobs) ===", flush=True)
        class MBI(ctypes.Structure):
            _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p),
                        ("AllocationProtect", wt.DWORD), ("RegionSize", ctypes.c_size_t),
                        ("State", wt.DWORD), ("Protect", wt.DWORD), ("Type", wt.DWORD)]
        mbi = MBI()
        addr = 0
        max_addr = 0x7FFEFFFF
        big = []
        while addr < max_addr:
            ret = kernel32.VirtualQueryEx(h, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi))
            if not ret:
                break
            try:
                base_int = ctypes.cast(mbi.BaseAddress, ctypes.c_void_p).value or 0
            except Exception:
                base_int = addr
            size_int = int(mbi.RegionSize)
            if mbi.State == 0x1000 and (mbi.Protect & 0xFF) in (0x04, 0x40):
                if size_int >= 256*1024:
                    # check if inside a known module; skip those
                    in_mod = any(b <= base_int < b+s for _, b, s in mods)
                    if not in_mod:
                        big.append((base_int, size_int, mbi.Protect))
            if size_int == 0:
                break
            addr = base_int + size_int
        big.sort(key=lambda x: -x[1])
        for b, s, p in big[:15]:
            print(f"  0x{b:08X} size=0x{s:X} ({s//1024}KB) prot=0x{p:X}", flush=True)
        # sample first bytes of top 3
        for b, s, p in big[:3]:
            d = read_mem(h, b, 256)
            if d:
                print(f"--- head of 0x{b:08X} ---", flush=True)
                hexdump(d, b, 256)
    finally:
        kernel32.CloseHandle(h)
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except Exception:
                proc.kill()
    print("[stage2 done]", flush=True)

if __name__ == "__main__":
    main()

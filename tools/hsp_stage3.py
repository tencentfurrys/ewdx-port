import ctypes
import ctypes.wintypes as wt
import subprocess
import time
import os
import struct

GAME_DIR = r"C:\Users\runneradmin\AppData\Local\Temp\2\echidna_extract\Echidna Wars DX [ENG-JAP]"
EXE_PATH = os.path.join(GAME_DIR, "EchidnaWarsDX.exe")

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
psapi = ctypes.WinDLL("psapi", use_last_error=True)
psapi.EnumProcessModulesEx.argtypes = [wt.HANDLE, ctypes.POINTER(wt.HMODULE), wt.DWORD, ctypes.POINTER(wt.DWORD), wt.DWORD]
psapi.EnumProcessModulesEx.restype = wt.BOOL
psapi.GetModuleFileNameExW.argtypes = [wt.HANDLE, wt.HMODULE, wt.LPWSTR, wt.DWORD]
psapi.GetModuleFileNameExW.restype = wt.DWORD
psapi.GetModuleInformation.argtypes = [wt.HANDLE, wt.HMODULE, wt.LPVOID, wt.DWORD]
psapi.GetModuleInformation.restype = wt.BOOL

LIST_MODULES_32BIT = 0x01
LIST_MODULES_64BIT = 0x02
LIST_MODULES_ALL = 0x03

class MODULEINFO(ctypes.Structure):
    _fields_ = [("lpBaseOfDll", wt.LPVOID), ("SizeOfImage", wt.DWORD), ("EntryPoint", wt.LPVOID)]

def read_mem(h, addr, size):
    buf = ctypes.create_string_buffer(size)
    br = ctypes.c_size_t(0)
    ok = kernel32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, size, ctypes.byref(br))
    if not ok or br.value == 0:
        return None
    return buf.raw[:br.value]

def enum_mods(h, flag, label):
    needed = wt.DWORD(0)
    arr = (wt.HMODULE * 1024)()
    ok = psapi.EnumProcessModulesEx(h, arr, ctypes.sizeof(arr), ctypes.byref(needed), flag)
    print(f"{label} ok={ok} err={ctypes.get_last_error()}", flush=True)
    if not ok:
        return []
    n = needed.value // ctypes.sizeof(wt.HMODULE)
    out = []
    for i in range(n):
        name_buf = ctypes.create_unicode_buffer(512)
        psapi.GetModuleFileNameExW(h, arr[i], name_buf, 512)
        mi = MODULEINFO()
        psapi.GetModuleInformation(h, arr[i], ctypes.byref(mi), ctypes.sizeof(mi))
        base = ctypes.cast(mi.lpBaseOfDll, ctypes.c_void_p).value or 0
        out.append((name_buf.value, base, mi.SizeOfImage))
    return out

def hexdump(data, base, limit=256):
    for off in range(0, min(len(data), limit), 16):
        c = data[off:off+16]
        print(f"  0x{base+off:08X}  {' '.join(f'{b:02X}' for b in c):<48}  {''.join(chr(b) if 32<=b<127 else '.' for b in c)}", flush=True)

def main():
    p = subprocess.Popen([EXE_PATH], cwd=GAME_DIR)
    print(f"pid={p.pid}", flush=True)
    time.sleep(6)
    if p.poll() is not None:
        print(f"exited rc={p.poll()}", flush=True)
        return
    h = kernel32.OpenProcess(0x0400 | 0x0010, False, p.pid)
    try:
        m32 = enum_mods(h, LIST_MODULES_32BIT, "32-bit")
        print(f"=== {len(m32)} 32-bit modules ===", flush=True)
        for name, base, size in m32:
            print(f"  0x{base:08X} +0x{size:X} {os.path.basename(name)}", flush=True)
        # string contexts in EXE image
        exebase = None
        for name, base, size in m32:
            if os.path.basename(name).lower() == "echidnawarsdx.exe":
                exebase = base
                break
        if exebase:
            for label, pat in [("start.ax", b"start.ax"), ("data.dpm", b"data.dpm"), ("hsp3", b"hsp3")]:
                # find in exebase..exebase+0x3A000 by reading whole image
                img = read_mem(h, exebase, 0x3A000)
                if not img:
                    print(f"read exe image failed", flush=True)
                    continue
                s = 0
                while True:
                    idx = img.find(pat, s)
                    if idx < 0:
                        break
                    a = exebase + idx
                    print(f"--- {label} at 0x{a:08X} ---", flush=True)
                    d = read_mem(h, a - 96, 288)
                    if d:
                        hexdump(d, a - 96, 288)
                    s = idx + 1
                    if s > len(img):
                        break
                break
        # scan hmm.dll image for interesting strings
        for name, base, size in m32:
            if os.path.basename(name).lower() == "hmm.dll":
                print(f"=== scanning hmm.dll 0x{base:08X}+0x{size:X} ===", flush=True)
                off = 0
                pats = [b"HSP3", b"DPM2", b"HSPHED", b"start.ax", b"HSP3Encode", b"Decrypt", b"crypt", b"dpm_"]
                while off < size:
                    sz = min(1024*1024, size - off)
                    d = read_mem(h, base + off, sz)
                    if not d:
                        break
                    for pat in pats:
                        s = 0
                        while True:
                            idx = d.find(pat, s)
                            if idx < 0:
                                break
                            print(f"  {pat} at 0x{base+off+idx:08X}", flush=True)
                            s = idx + 1
                    off += sz
                break
        # heap scan for HSP3 with relaxed check + context
        print("=== heap scan HSP3 (any version) with context ===", flush=True)
        class MBI(ctypes.Structure):
            _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p),
                        ("AllocationProtect", wt.DWORD), ("RegionSize", ctypes.c_size_t),
                        ("State", wt.DWORD), ("Protect", wt.DWORD), ("Type", wt.DWORD)]
        mbi = MBI()
        addr = 0
        hits = 0
        while addr < 0x7FFEFFFF and hits < 10:
            ret = kernel32.VirtualQueryEx(h, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi))
            if not ret:
                break
            try:
                b = ctypes.cast(mbi.BaseAddress, ctypes.c_void_p).value or 0
            except Exception:
                b = addr
            s = int(mbi.RegionSize)
            if mbi.State == 0x1000 and (mbi.Protect & 0xFF) in (0x02, 0x04, 0x08, 0x20, 0x40, 0x80):
                in_mod = any(mb <= b < mb+ms for _, mb, ms in m32)
                if not in_mod and s >= 4096:
                    off2 = 0
                    stop = False
                    while off2 < s and not stop:
                        sz = min(2*1024*1024, s - off2)
                        d = read_mem(h, b + off2, sz)
                        if not d:
                            break
                        st = 0
                        while True:
                            idx = d.find(b"HSP3", st)
                            if idx < 0:
                                break
                            a = b + off2 + idx
                            ver = struct.unpack_from("<I", d, idx+4)[0] if idx+8 <= len(d) else 0
                            print(f"  HSP3 at 0x{a:08X} ver=0x{ver:08X} region=0x{b:08X}", flush=True)
                            ctx = read_mem(h, a - 32, 160)
                            if ctx:
                                hexdump(ctx, a - 32, 160)
                            hits += 1
                            if hits >= 10:
                                stop = True
                                break
                            st = idx + 1
                        off2 += sz
            if s == 0:
                break
            addr = b + s
        print(f"heap HSP3 hits={hits}", flush=True)
    finally:
        kernel32.CloseHandle(h)
        if p.poll() is None:
            p.terminate()
            try:
                p.wait(timeout=5)
            except Exception:
                p.kill()
    print("[stage3 done]", flush=True)

if __name__ == "__main__":
    main()

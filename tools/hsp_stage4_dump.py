import ctypes
import ctypes.wintypes as wt
import subprocess
import time
import os
import struct

GAME_DIR = r"C:\Users\runneradmin\AppData\Local\Temp\2\echidna_extract\Echidna Wars DX [ENG-JAP]"
EXE_PATH = os.path.join(GAME_DIR, "EchidnaWarsDX.exe")
OUT_DIR = r"C:\Users\RUNNER~1\AppData\Local\Temp\2\opencode"
DUMP_PATH = os.path.join(OUT_DIR, "start_ax_dump.bin")

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

def read_mem(h, addr, size):
    buf = ctypes.create_string_buffer(size)
    br = ctypes.c_size_t(0)
    ok = kernel32.ReadProcessMemory(h, ctypes.c_void_p(addr), buf, size, ctypes.byref(br))
    if not ok or br.value == 0:
        return None
    return buf.raw[:br.value]

def validate(buf, off):
    if off + 72 > len(buf):
        return None
    if buf[off:off+4] != b"HSP3":
        return None
    version, max_val, allsize, pt_cs, max_cs, pt_ds, max_ds = struct.unpack_from("<7I", buf, off+4)
    if version != 0x301:
        return None
    if max_val > 100000 or max_val == 0:
        return None
    if not (10000 < allsize < 20_000_000):
        return None
    if pt_cs >= allsize or max_cs >= allsize or pt_ds >= allsize or max_ds >= allsize:
        return None
    if pt_cs + max_cs != pt_ds:  # strong structural check
        return None
    return {"version": hex(version), "max_val": max_val, "allsize": allsize,
            "pt_cs": pt_cs, "max_cs": max_cs, "pt_ds": pt_ds, "max_ds": max_ds}

def main():
    p = subprocess.Popen([EXE_PATH], cwd=GAME_DIR)
    print(f"pid={p.pid}", flush=True)
    time.sleep(7)
    if p.poll() is not None:
        print(f"exited rc={p.poll()}", flush=True)
        return
    h = kernel32.OpenProcess(0x0400 | 0x0010, False, p.pid)
    try:
        class MBI(ctypes.Structure):
            _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p),
                        ("AllocationProtect", wt.DWORD), ("RegionSize", ctypes.c_size_t),
                        ("State", wt.DWORD), ("Protect", wt.DWORD), ("Type", wt.DWORD)]
        mbi = MBI()
        addr = 0
        dumped = False
        while addr < 0x7FFEFFFF and not dumped:
            ret = kernel32.VirtualQueryEx(h, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi))
            if not ret:
                break
            try:
                b = ctypes.cast(mbi.BaseAddress, ctypes.c_void_p).value or 0
            except Exception:
                b = addr
            s = int(mbi.RegionSize)
            if mbi.State == 0x1000 and (mbi.Protect & 0xFF) in (0x02, 0x04, 0x08, 0x20, 0x40, 0x80) and s >= 4096:
                off = 0
                while off < s and not dumped:
                    sz = min(2*1024*1024, s - off)
                    d = read_mem(h, b + off, sz)
                    if not d:
                        break
                    st = 0
                    while True:
                        idx = d.find(b"HSP3", st)
                        if idx < 0:
                            break
                        info = validate(d, idx)
                        if info:
                            a = b + off + idx
                            print(f"HIT at 0x{a:08X} {info}", flush=True)
                            data = read_mem(h, a, info["allsize"])
                            if data and len(data) == info["allsize"]:
                                with open(DUMP_PATH, "wb") as f:
                                    f.write(data)
                                print(f"WROTE {DUMP_PATH} {len(data)} bytes", flush=True)
                                dumped = True
                                break
                            else:
                                print(f"dump failed got={len(data) if data else None}", flush=True)
                        st = idx + 1
                    off += sz
            if s == 0:
                break
            addr = b + s
        print(f"dumped={dumped}", flush=True)
    finally:
        kernel32.CloseHandle(h)
        if p.poll() is None:
            p.terminate()
            try:
                p.wait(timeout=5)
            except Exception:
                p.kill()
    print("[stage4 done]", flush=True)

if __name__ == "__main__":
    main()

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
DUMP_PATH = os.path.join(OUT_DIR, "memdump_ax.bin")

kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)

PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
MEM_COMMIT = 0x1000
PAGE_NOACCESS = 0x01
PAGE_GUARD = 0x100

class MEMORY_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("BaseAddress", ctypes.c_void_p),
        ("AllocationBase", ctypes.c_void_p),
        ("AllocationProtect", wt.DWORD),
        ("RegionSize", ctypes.c_size_t),
        ("State", wt.DWORD),
        ("Protect", wt.DWORD),
        ("Type", wt.DWORD),
    ]

def is_readable(protect):
    if protect & PAGE_GUARD:
        return False
    p = protect & 0xFF
    # PAGE_READONLY=0x02, READWRITE=0x04, WRITECOPY=0x08, EXECUTE_READ=0x20, EXECUTE_READWRITE=0x40, EXECUTE_WRITECOPY=0x80
    return p in (0x02, 0x04, 0x08, 0x20, 0x40, 0x80)

def validate_hsp3_header(buf, off):
    # need 72 bytes
    if off + 72 > len(buf):
        return None
    if buf[off:off+4] != b"HSP3":
        return None
    try:
        version, max_val, allsize, pt_cs, max_cs, pt_ds, max_ds = struct.unpack_from("<7I", buf, off+4)
    except Exception:
        return None
    if not (0x3000 <= version < 0x4000):
        return None
    if not (72 < allsize < 50_000_000):
        return None
    if not (0 <= pt_cs < allsize and 0 < max_cs < allsize):
        return None
    if not (0 <= pt_ds < allsize and 0 < max_ds < allsize):
        return None
    if max_val > 100000:
        return None
    return {"version": hex(version), "max_val": max_val, "allsize": allsize,
            "pt_cs": pt_cs, "max_cs": max_cs, "pt_ds": pt_ds, "max_ds": max_ds}

def scan_process(pid, handle):
    print(f"[scan] pid={pid} handle={handle}", flush=True)
    mbi = MEMORY_BASIC_INFORMATION()
    addr = 0
    max_addr = 0x7FFEFFFF
    found = []
    regions_scanned = 0
    # ReadProcessMemory setup
    ReadProcessMemory = kernel32.ReadProcessMemory
    ReadProcessMemory.argtypes = [wt.HANDLE, wt.LPCVOID, wt.LPVOID, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    ReadProcessMemory.restype = wt.BOOL
    VirtualQueryEx = kernel32.VirtualQueryEx
    VirtualQueryEx.argtypes = [wt.HANDLE, wt.LPCVOID, ctypes.POINTER(MEMORY_BASIC_INFORMATION), ctypes.c_size_t]
    VirtualQueryEx.restype = ctypes.c_size_t

    CHUNK = 4 * 1024 * 1024
    while addr < max_addr:
        ret = VirtualQueryEx(handle, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi))
        if not ret:
            break
        region_base = mbi.BaseAddress
        region_size = mbi.RegionSize
        # advance
        next_addr = (region_base if isinstance(region_base, int) else region_base or 0)
        # ctypes c_void_p value
        try:
            base_int = ctypes.cast(mbi.BaseAddress, ctypes.c_void_p).value or 0
        except Exception:
            base_int = addr
        size_int = int(mbi.RegionSize)
        if mbi.State == MEM_COMMIT and is_readable(mbi.Protect):
            # read in chunks
            offset = 0
            while offset < size_int:
                to_read = min(CHUNK + 8, size_int - offset)
                buf = ctypes.create_string_buffer(to_read)
                bytes_read = ctypes.c_size_t(0)
                ok = ReadProcessMemory(handle, ctypes.c_void_p(base_int + offset), buf, to_read, ctypes.byref(bytes_read))
                if not ok or bytes_read.value == 0:
                    break
                data = buf.raw[:bytes_read.value]
                # search HSP3
                start = 0
                while True:
                    idx = data.find(b"HSP3", start)
                    if idx < 0:
                        break
                    abs_addr = base_int + offset + idx
                    info = validate_hsp3_header(data, idx)
                    if info:
                        print(f"[HIT] HSP3 candidate at 0x{abs_addr:08X} region_base=0x{base_int:08X} {info}", flush=True)
                        found.append((abs_addr, base_int + offset + idx, info))
                    # also log any HSP3 even if invalid
                    else:
                        # quick peek version
                        if idx + 8 <= len(data):
                            ver = struct.unpack_from("<I", data, idx+4)[0]
                            print(f"[peek] HSP3 magic at 0x{abs_addr:08X} ver=0x{ver:08X}", flush=True)
                    start = idx + 1
                # search DPM2
                s2 = 0
                while True:
                    idx2 = data.find(b"DPM2", s2)
                    if idx2 < 0:
                        break
                    abs2 = base_int + offset + idx2
                    print(f"[DPM2] at 0x{abs2:08X}", flush=True)
                    found.append((abs2, abs2, {"type": "DPM2"}))
                    s2 = idx2 + 1
                # search HSPHED~~
                s3 = 0
                while True:
                    idx3 = data.find(b"HSPHED~~", s3)
                    if idx3 < 0:
                        break
                    abs3 = base_int + offset + idx3
                    print(f"[HSPHED~~] at 0x{abs3:08X}", flush=True)
                    s3 = idx3 + 1
                offset += bytes_read.value
                regions_scanned += 1
                if regions_scanned % 50 == 0:
                    print(f"[scan] progress base=0x{base_int:08X} size=0x{size_int:X}", flush=True)
        # move addr
        if size_int == 0:
            break
        addr = base_int + size_int
    print(f"[scan] done regions chunks={regions_scanned} hits={len(found)}", flush=True)
    return found, handle

def main():
    print(f"Launching {EXE_PATH}", flush=True)
    print(f"cwd={GAME_DIR}", flush=True)
    if not os.path.exists(EXE_PATH):
        print("EXE NOT FOUND", flush=True)
        sys.exit(2)
    # Launch game; keep output
    try:
        proc = subprocess.Popen([EXE_PATH], cwd=GAME_DIR,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except Exception as e:
        print(f"launch failed: {e}", flush=True)
        sys.exit(3)
    print(f"pid={proc.pid}", flush=True)
    # Give it time to init/decrypt: scan at 3s, 8s, 15s
    OpenProcess = kernel32.OpenProcess
    OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
    OpenProcess.restype = wt.HANDLE
    all_hits = []
    for wait_s in (3, 5, 7):
        time.sleep(wait_s)
        rc = proc.poll()
        print(f"[wait] +{wait_s}s poll={rc}", flush=True)
        if rc is not None:
            print(f"process exited early rc={rc}", flush=True)
            try:
                out = proc.communicate(timeout=5)[0]
                print(out[:4000] if out else b"<no output>", flush=True)
            except Exception as e:
                print(f"communicate err {e}", flush=True)
            break
        h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, False, proc.pid)
        if not h:
            print(f"OpenProcess failed err={ctypes.get_last_error()}", flush=True)
            continue
        try:
            hits, _ = scan_process(proc.pid, h)
            all_hits.extend(hits)
            if hits:
                # try dump first valid HSP3 AX
                for abs_addr, _, info in hits:
                    if isinstance(info, dict) and "allsize" in info:
                        allsize = info["allsize"]
                        print(f"[dump] attempting dump allsize={allsize} from 0x{abs_addr:08X}", flush=True)
                        # read allsize bytes via ReadProcessMemory
                        buf = ctypes.create_string_buffer(allsize)
                        br = ctypes.c_size_t(0)
                        ok = kernel32.ReadProcessMemory(h, ctypes.c_void_p(abs_addr), buf, allsize, ctypes.byref(br))
                        print(f"[dump] ReadProcessMemory ok={ok} read={br.value}", flush=True)
                        if ok and br.value == allsize:
                            with open(DUMP_PATH, "wb") as f:
                                f.write(buf.raw)
                            print(f"[dump] WROTE {DUMP_PATH} {allsize} bytes", flush=True)
                            # don't kill yet, continue scanning once
                        break
        finally:
            kernel32.CloseHandle(h)
        if os.path.exists(DUMP_PATH) and os.path.getsize(DUMP_PATH) > 72:
            print("[done] dump exists, breaking", flush=True)
            break
    # cleanup
    try:
        if proc.poll() is None:
            print("[cleanup] terminating game", flush=True)
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except Exception:
                proc.kill()
    except Exception as e:
        print(f"cleanup err {e}", flush=True)
    print(f"HITS total={len(all_hits)}", flush=True)
    for h_addr, _, info in all_hits[:20]:
        print(f"  0x{h_addr:08X} {info}", flush=True)

if __name__ == "__main__":
    main()

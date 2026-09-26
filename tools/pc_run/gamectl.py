"""gamectl.py - drive the PC EchidnaWarsDX.exe window (spy capture sessions).

Usage:
  python tools/pc_run/gamectl.py move X Y          # move window to X,Y
  python tools/pc_run/gamectl.py grab out.png      # desktop-grab the window rect
  python tools/pc_run/gamectl.py shot out.png      # PrintWindow (black on D3D9)
  python tools/pc_run/gamectl.py key K down|up
  python tools/pc_run/gamectl.py tap K [ms]
  python tools/pc_run/gamectl.py info
"""
import sys, time, ctypes
from ctypes import wintypes

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32

def _vk(name):
    table = {
        "UP": 0x26, "DOWN": 0x28, "LEFT": 0x25, "RIGHT": 0x27,
        "SPACE": 0x20, "ENTER": 0x0D, "ESC": 0x1B, "CTRL": 0x11,
        "SHIFT": 0x10, "TAB": 0x09, "Z": 0x5A, "X": 0x58, "C": 0x43, "V": 0x56,
        "A": 0x41, "S": 0x53, "D": 0x44, "W": 0x57,
    }
    if name.upper() in table:
        return table[name.upper()]
    if len(name) == 1:
        return ord(name.upper())
    raise ValueError(name)

TARGET_PID = None

def find_window():
    hits = []
    @ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
    def cb(h, l):
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(h, ctypes.byref(pid))
        if pid.value == TARGET_PID and user32.IsWindowVisible(h):
            n = ctypes.create_unicode_buffer(256)
            user32.GetWindowTextW(h, n, 256)
            cls = ctypes.create_unicode_buffer(256)
            user32.GetClassNameW(h, cls, 256)
            hits.append((h, n.value, cls.value))
        return True
    user32.EnumWindows(cb, 0)
    return hits

def game_window():
    import subprocess
    out = subprocess.check_output(
        ["tasklist", "/FI", "IMAGENAME eq EchidnaWarsDX.exe", "/FO", "CSV"]).decode(errors="ignore")
    pids = [int(l.split('","')[1]) for l in out.strip().splitlines()[1:] if '","' in l]
    if not pids:
        raise SystemExit("game not running")
    global TARGET_PID
    TARGET_PID = pids[0]
    wins = find_window()
    if not wins:
        raise SystemExit("no visible window for pid %d" % TARGET_PID)
    # prefer the hspwnd0 game window over dialogs
    for h, t, c in wins:
        if c.startswith("hspwnd"):
            return h, t, c
    return wins[0]

def focus(hwnd):
    user32.SetForegroundWindow(hwnd)
    time.sleep(0.12)

def rect(hwnd):
    r = wintypes.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    p = wintypes.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(p))
    return p.x, p.y, r.right, r.bottom

def keybd(vk, down):
    PUL = ctypes.POINTER(ctypes.c_ulong)
    class KEYBDINPUT(ctypes.Structure):
        _fields_ = [("wVk", ctypes.c_ushort), ("wScan", ctypes.c_ushort),
                    ("dwFlags", ctypes.c_ulong), ("time", ctypes.c_ulong),
                    ("dwExtraInfo", PUL)]
    class INPUT(ctypes.Structure):
        class _I(ctypes.Union):
            _fields_ = [("ki", KEYBDINPUT), ("pad", ctypes.c_ubyte * 32)]
        _anonymous_ = ("i",); _fields_ = [("type", ctypes.c_ulong), ("i", _I)]
    inp = INPUT(1)
    inp.ki = KEYBDINPUT(vk, 0, 0 if down else 2, 0, None)
    user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))

def desktop_grab(hwnd, path):
    from PIL import ImageGrab
    x, y, w, h = rect(hwnd)
    x0, y0 = max(x, 0), max(y, 0)
    im = ImageGrab.grab(bbox=(x0, y0, min(x + w, user32.GetSystemMetrics(0)),
                              min(y + h, user32.GetSystemMetrics(1))))
    im.save(path)
    print("grab ->", path, im.size)

def screenshot(hwnd, path):
    x, y, w, h = rect(hwnd)
    hdc = user32.GetWindowDC(hwnd)
    mem = gdi32.CreateCompatibleDC(hdc)
    bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
    gdi32.SelectObject(mem, bmp)
    ok = user32.PrintWindow(hwnd, mem, 3)
    import ctypes.wintypes as wt
    class BMIH(ctypes.Structure):
        _fields_ = [("biSize", wt.DWORD), ("biWidth", wt.LONG), ("biHeight", wt.LONG),
                    ("biPlanes", wt.WORD), ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                    ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", wt.LONG), ("biYPelsPerMeter", wt.LONG),
                    ("biClrUsed", wt.DWORD), ("biClrImportant", wt.DWORD)]
    bmi = BMIH(ctypes.sizeof(BMIH), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
    buf = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(bmi), 0)
    import struct, zlib
    rows = []
    for i in range(h):
        row = buf.raw[i * w * 4:(i + 1) * w * 4]
        px = bytearray()
        for j in range(w):
            b, g, r, a = row[j * 4:j * 4 + 4]
            px += bytes((r, g, b))
        rows.append(b"\x00" + bytes(px))
    raw = b"".join(rows)
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 6))
           + chunk(b"IEND", b""))
    open(path, "wb").write(png)
    gdi32.DeleteObject(bmp); gdi32.DeleteDC(mem); user32.ReleaseDC(hwnd, hdc)
    return ok

def main():
    hwnd, title, cls = game_window()
    cmd = sys.argv[1] if len(sys.argv) > 1 else "info"
    if cmd == "info":
        print("hwnd=0x%X title=%r class=%r rect=%s" % (hwnd, title, cls, rect(hwnd)))
    elif cmd == "move":
        x, y = int(sys.argv[2]), int(sys.argv[3])
        user32.SetWindowPos(hwnd, 0, x, y, 0, 0, 0x0001 | 0x0004)  # NOSIZE|NOZORDER
        time.sleep(0.1)
        print("moved ->", rect(hwnd))
    elif cmd == "grab":
        focus(hwnd)
        desktop_grab(hwnd, sys.argv[2])
    elif cmd == "shot":
        focus(hwnd)
        ok = screenshot(hwnd, sys.argv[2])
        print("shot ->", sys.argv[2], "PrintWindow ok:", ok)
    elif cmd == "key":
        vk = _vk(sys.argv[2]); down = sys.argv[3] == "down"
        focus(hwnd)
        keybd(vk, down)
        print("key", sys.argv[2], sys.argv[3])
    elif cmd == "tap":
        vk = _vk(sys.argv[2])
        ms = float(sys.argv[3]) if len(sys.argv) > 3 else 80.0
        focus(hwnd)
        keybd(vk, True); time.sleep(ms / 1000.0); keybd(vk, False)
        print("tapped", sys.argv[2])
    return 0

if __name__ == "__main__":
    main()

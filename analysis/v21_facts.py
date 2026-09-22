#!/usr/bin/env python3
# v21 facts: the maw mask tile lives in system.bmp at (242,72,40,40) per the
# script's DGRECT before DGGCOPY 3. Print it as ASCII art + stats so the disc
# orientation (and hence the correct blend hypothesis) is pinned by pixels.
import numpy as np, cv2, os, subprocess, sys

here = os.path.dirname(os.path.abspath(__file__))

def find_bmp():
    cands = [
        r"C:/Users/RUNNER~1/AppData/Local/Temp/2/ewdx_v20/assets/data/pic/system.bmp",
        "/tmp/ewdx_v20/assets/data/pic/system.bmp",
    ]
    for c in cands:
        if os.path.exists(c):
            return c
    try:
        w = subprocess.run(["cygpath", "-w", "/tmp/ewdx_v20/assets/data/pic/system.bmp"],
                           capture_output=True, text=True).stdout.strip().replace("\\", "/")
        if os.path.exists(w):
            return w
    except Exception:
        pass
    return None

p = find_bmp()
print("system.bmp:", p)
if not p:
    sys.exit("system.bmp not found - extract the v20 APK assets first")

raw = cv2.imread(p, cv2.IMREAD_UNCHANGED)
print("image:", None if raw is None else raw.shape, raw.dtype if raw is not None else "")
assert raw is not None
if raw.ndim == 3 and raw.shape[2] == 4:
    b, g, r, a = cv2.split(raw)
    gray = cv2.merge([r, g, b]).mean(axis=2)
    print("has alpha: min %d max %d  (alpha==0 px: %.1f%%)" % (a.min(), a.max(), (a == 0).mean() * 100))
else:
    gray = (raw if raw.ndim == 2 else cv2.cvtColor(raw, cv2.COLOR_BGR2GRAY)).astype(np.float32)

tile = gray[72:112, 242:282]
print("\ntile (242,72)-(282,112): min %.0f max %.0f mean %.1f" % (tile.min(), tile.max(), tile.mean()))
print("corners TL,TR,BL,BR: %.0f %.0f %.0f %.0f   center: %.0f" % (
    tile[1, 1], tile[1, -2], tile[-2, 1], tile[-2, -2], tile[20, 20]))

# ASCII art: 40x40 -> 20x20 blocks of 2x2
print("\nASCII art (# = dark <80, + = mid 80..180, . = light >=180):")
for y in range(0, 40, 2):
    row = ""
    for x in range(0, 40, 2):
        v = tile[y:y + 2, x:x + 2].mean()
        row += "#" if v < 80 else ("+" if v < 180 else ".")
    print(row)

# radius profile from each corner to see which corner hosts the disc
print("\nmean brightness along diagonals from each corner (0..56 px):")
for name, (dy, dx) in (("TL", (1, 1)), ("TR", (1, -1)), ("BL", (-1, 1)), ("BR", (-1, -1))):
    vals = []
    for d in range(0, 40, 4):
        y, x = (d, d) if name == "TL" else (d, 39 - d) if name == "TR" else \
               (39 - d, d) if name == "BL" else (39 - d, 39 - d)
        vals.append(tile[max(0, min(39, y)), max(0, min(39, x))])
    print("  %s: %s" % (name, " ".join("%3.0f" % v for v in vals)))

big = cv2.resize(tile, (320, 320), interpolation=cv2.INTER_NEAREST)
cv2.imwrite(os.path.join(here, "v21_tile_big.png"), big)
print("\nwrote v21_tile_big.png (320x320 nearest-neighbor)")

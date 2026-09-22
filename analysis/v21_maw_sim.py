#!/usr/bin/env python3
# Extract the maw mask tile system.bmp (242,72,40,40) from the v20 APK assets,
# build the 4x-mirrored 80x80 mask exactly like the script (DGGCOPY 3 / 8 / 16 / 24),
# then simulate the buffer-5 maw composite under both DGBLENDMODE-3 hypotheses:
#   A (port today): mode3 = (ZERO, INVSRCCOLOR) -> buf5 *= (1 - mask)
#   B (swap):       mode3 = (ZERO, SRCCOLOR)     -> buf5 *= mask
# Buffer 5 starts WHITE (DGCLEAR after DGCOLOR 255,255,255,255), the interior
# sprite is drawn, then the mask pass. Finally buffer 5 is blitted opaquely
# (DGGCOPY 5,1) over the scene like the capture.
import numpy as np, cv2, os

here = os.path.dirname(__file__)
apk_bmp = "C:/Users/RUNNER~1/AppData/Local/Temp/2/ewdx_v20/assets/data/pic/system.bmp"
if not os.path.exists(apk_bmp):
    import subprocess
    apk_bmp = subprocess.run(["cygpath", "-w", "/tmp/ewdx_v20/assets/data/pic/system.bmp"],
                             capture_output=True, text=True).stdout.strip().replace("\\", "/")
    print("fallback path:", apk_bmp, os.path.exists(apk_bmp))

raw = cv2.imread(apk_bmp, cv2.IMREAD_UNCHANGED)
print("system.bmp:", None if raw is None else raw.shape, raw.dtype if raw is not None else "")
assert raw is not None and raw.ndim == 3 and raw.shape[2] in (3, 4)
if raw.shape[2] == 4:
    b, g, r, a = cv2.split(raw)
    rgb = cv2.merge([r, g, b]).astype(np.float32)
    if a.min() == 0:
        rgb_mask = 255.0 - rgb   # colorkey black -> opaque mask (port behavior)
        print("alpha channel present but has zeros -> colorkey inversion applied")
    else:
        rgb_mask = a.astype(np.float32)
        print("alpha channel present and valid -> using alpha as mask")
else:
    rgb = cv2.cvtColor(raw, cv2.COLOR_BGR2RGB).astype(np.float32)
    rgb_mask = 255.0 - rgb    # colorkey black -> opaque mask (port behavior)
    print("no alpha -> colorkey inversion applied")

tile = rgb_mask[72:112, 242:282]
tile_g = tile.mean(axis=2)
print("tile stats: min %.0f max %.0f mean %.1f" % (tile.min(), tile.max(), tile.mean()))
print("tile corners TL,TR,BL,BR: %4.0f %4.0f %4.0f %4.0f  center: %4.0f" % (
    tile_g[2, 2], tile_g[2, -3], tile_g[-3, 2], tile_g[-3, -3], tile_g[20, 20]))
cv2.imwrite(os.path.join(here, "v21_tile_extract.png"), tile_g)

# 4x mirror tile to 80x80 like DGGCOPY 3 / 8 / 16 / 24
m = np.zeros((80, 80), np.float32)
m[0:40, 0:40] = tile_g
m[0:40, 40:80] = tile_g[:, ::-1]
m[40:80, 0:40] = tile_g[::-1, :]
m[40:80, 40:80] = tile_g[::-1, ::-1]
cv2.imwrite(os.path.join(here, "v21_mask80.png"), m)

# --- synthetic scene (320x240 logical, like buffers) ----------------------
H, W = 240, 320
backdrop = np.zeros((H, W, 3), np.float32)
backdrop[..., 0] = 30; backdrop[..., 1] = 24; backdrop[..., 2] = 44   # night sky
backdrop[140:200, 200:260] = (80, 200, 60)     # enemy green
backdrop[150:195, 90:130] = (210, 175, 155)    # player peach
backdrop[100:180, 40:80] = (95, 95, 105)       # building grey
backdrop[30:80, 250:310] = (60, 60, 70)        # far building

interior = np.zeros((H, W, 3), np.float32)     # faint pink sprite in the disc
cv2.circle(interior, (60, 60), 12, (215, 130, 150), -1)
interior = (interior * 0.6).astype(np.float32)

white = np.full((H, W, 3), 255.0, np.float32)
buf5 = np.maximum(white, interior)             # sprite over white, opaque
m3 = (m / 255.0)[..., None]
b5_A = buf5 * (1.0 - m3)      # hypo A: invert-multiply (port today)
b5_B = buf5 * m3              # hypo B: multiply (swap hypothesis)

def blit_opaque(dst, src):    # DGGCOPY 5,1 opaque blit of the 80x80 region
    out = dst.copy()
    out[40:120, 20:100] = src[40:120, 20:100] if src.shape[0] > 120 else src
    return out

# the 80x80 quad sits at (stom_x, stom_y) in the scene; center it like the video
comp_A = backdrop.copy(); comp_A[60:140, 40:120] = b5_A[40:120, 40:120][0:80, 0:80]
comp_B = backdrop.copy(); comp_B[60:140, 40:120] = b5_B[40:120, 40:120][0:80, 0:80]

for name, img in (("A_invert_port_today", comp_A), ("B_multiply_swap", comp_B)):
    p = os.path.join(here, "v21_sim_%s.png" % name)
    cv2.imwrite(p, img[..., ::-1])
    reg = img[60:140, 40:120]
    print("%-22s mean %.1f  near-black(<40) %.0f%%" % (
        name, reg.mean(), (reg.max(axis=2) < 40).mean() * 100))
print("wrote v21_sim_A_invert_port_today.png / v21_sim_B_multiply_swap.png")

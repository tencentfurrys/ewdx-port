# v26 session: locate maw/POV scenes in owner videos (PC reference vs Android port).
# Numeric-only segmentation: mean RGB, near-black fraction, red fraction (central crop).
import cv2, numpy as np, os, sys

BASE = os.path.expanduser("~/Downloads/incoming")

def rows_for(d):
    rows = []
    files = sorted(f for f in os.listdir(d) if f.endswith(".jpg"))
    for i, f in enumerate(files):
        img = cv2.imread(os.path.join(d, f))
        h, w = img.shape[:2]
        c = img[40:h-40, 107:w-107]  # central crop, drop side/top bars
        b, g, r = c[:,:,0].astype(np.int32), c[:,:,1].astype(np.int32), c[:,:,2].astype(np.int32)
        lum = (r*2 + g*3 + b) // 6
        black = float((lum < 40).mean())
        red = float(((r > 110) & (r > 1.35*g) & (r > 1.35*b)).mean())
        mean = (float(r.mean()), float(g.mean()), float(b.mean()))
        rows.append((i, f, mean, black, red))
    return rows

def segprint(name, rows):
    print(f"===== {name} ({len(rows)} frames @2fps) =====")
    prev = None
    for i, f, mean, black, red in rows:
        key = (round(black,1), round(red,1))
        changed = prev is None or (abs(black - prev[0]) > 12 or abs(red - prev[1]) > 12)
        if changed or i % 20 == 0:
            t = i / 2.0
            print(f"  t={t:6.1f}s f{i:03d} meanRGB=({mean[0]:5.1f},{mean[1]:5.1f},{mean[2]:5.1f}) black%={black:5.1f} red%={red:5.1f}")
        prev = key

pc = rows_for(os.path.join(BASE, "frames/pc"))
an = rows_for(os.path.join(BASE, "frames/and"))
segprint("PC", pc)
segprint("ANDROID", an)

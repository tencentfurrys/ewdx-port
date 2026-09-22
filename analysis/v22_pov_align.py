#!/usr/bin/env python3
# Phase-aligned POV interior comparison (session E, part 2).
# Aligns each video at its swallow moment (first sustained dark-plate sample
# after the last bright frame) and compares the interior evolution frame by
# frame: bright/pink fractions, mean RGB, connected-part counts.
# Writes analysis/v22_pov/ALIGNED_<tag>_<phase>s.jpg crops and a combined
# PHASES_sheet.jpg (rows = videos, cols = t0+0..t0+11s).
import cv2, os
import numpy as np

VIDEOS = [
    ("ref",  r"C:/Users/RDP/Downloads/2026_09_22_17_00_09.mp4"),
    ("mumu1", r"C:/Users/RDP/Documents/MuMuSharedFolder/VideoRecords/EchidnaWarsDX(1).mp4"),
    ("mumu2", r"C:/Users/RDP/Documents/MuMuSharedFolder/VideoRecords/EchidnaWarsDX(2).mp4"),
]
here = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(here, "v22_pov")
os.makedirs(out, exist_ok=True)

CX, CY = 480, 270
PLATE, DISC = 100, 44

def center_detect(cap):
    n = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    idxs = np.linspace(0, max(0, n - 1), 25).astype(int)
    med = None; cnt = 0
    for i in idxs:
        cap.set(cv2.CAP_PROP_POS_FRAMES, int(i))
        ok, img = cap.read()
        if ok:
            g = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
            med = g if med is None else med + g; cnt += 1
    cap.set(cv2.CAP_PROP_POS_FRAMES, 0)
    med = (med / cnt).astype(np.uint8)
    xs = np.where(med.mean(axis=0) > 12)[0]
    ys = np.where(med.mean(axis=1) > 12)[0]
    return (int((xs[0] + xs[-1]) // 2), int((ys[0] + ys[-1]) // 2)) if len(xs) and len(ys) else (CX, CY)

def sample(cap, cx, cy):
    ok, img = cap.read()
    if not ok:
        return None, None
    g = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
    box = g[max(0, cy - PLATE):cy + PLATE, max(0, cx - PLATE):cx + PLATE]
    dark = float((box < 60).mean())
    yy, xx = np.ogrid[:img.shape[0], :img.shape[1]]
    dm = ((xx - cx) ** 2 + (yy - cy) ** 2) <= DISC * DISC
    V = hsv[:, :, 2]
    bright = dm & (V > 110)
    m = (bright * 255).astype(np.uint8)
    m = cv2.morphologyEx(m, cv2.MORPH_OPEN, np.ones((3, 3), np.uint8))
    nlab, lab, stats, _ = cv2.connectedComponentsWithStats(m, 8)
    parts = sum(1 for i in range(1, nlab) if stats[i, cv2.CC_STAT_AREA] >= 14)
    mb = img[dm]
    mean = mb.mean(axis=0) if len(mb) else np.zeros(3)
    return dict(dark=dark, bright=float(bright.mean()), parts=parts,
                mean=mean, sat=float(hsv[:, :, 1][dm].mean())), img

rows = {}
for tag, path in VIDEOS:
    cap = cv2.VideoCapture(path)
    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    cx, cy = center_detect(cap)
    seq = []
    f = 0
    while True:
        m, _ = sample(cap, cx, cy)
        if m is None: break
        m["f"] = f; seq.append(m); f += 1
    cap.release()
    # swallow = first sample of the longest dark run that FOLLOWS a bright frame
    # (the maw opens bright-ish, then the plate covers everything)
    dark = [m["dark"] > 0.55 for m in seq]
    best = (0, None); run = 0; start = None
    for i, d in enumerate(dark):
        if d:
            if run == 0: start = i
            run += 1
            if run > best[0]: best = (run, start)
        else:
            run = 0
    t0 = best[1]
    rows[tag] = dict(seq=seq, t0=t0, fps=fps, cx=cx, cy=cy, path=path)
    print("%s: swallow-aligned t0 = sample %d (t=%.1fs), dark run %d samples (~%.1fs)" % (
        tag, t0, t0 / fps, best[0], best[0] / fps))
    print("   t0-1s: bright=%.4f parts=%d | t0: dark=%.2f | +2s: dark=%.2f bright=%.4f parts=%d | +6s: dark=%.2f bright=%.4f parts=%d sat=%.0f" % (
        seq[t0 - 30]["bright"], seq[t0 - 30]["parts"],
        seq[t0]["dark"],
        seq[min(len(seq) - 1, t0 + 60)]["dark"], seq[min(len(seq) - 1, t0 + 60)]["bright"], seq[min(len(seq) - 1, t0 + 60)]["parts"],
        seq[min(len(seq) - 1, t0 + 180)]["dark"], seq[min(len(seq) - 1, t0 + 180)]["bright"], seq[min(len(seq) - 1, t0 + 180)]["parts"], seq[min(len(seq) - 1, t0 + 180)]["sat"]))

# ---- aligned crop sheet: rows=videos, cols=t0+{0,1,2,4,6,8,10,12}s ----
PHASES = [0, 30, 60, 120, 180, 240, 300, 360]   # frames @30fps
CELL = 300
grid = []
for tag, r in rows.items():
    cap = cv2.VideoCapture(r["path"])
    cells = []
    for ph in PHASES:
        i = min(len(r["seq"]) - 1, r["t0"] + ph)
        m = r["seq"][i]
        cap.set(cv2.CAP_PROP_POS_FRAMES, m["f"])
        ok, img = cap.read()
        if not ok:
            img = np.zeros((CELL, CELL, 3), np.uint8)
        half = 95
        x0, x1 = max(0, r["cx"] - half), min(img.shape[1], r["cx"] + half)
        y0, y1 = max(0, r["cy"] - half), min(img.shape[0], r["cy"] + half)
        z = img[y0:y1, x0:x1]
        z = cv2.resize(z, (CELL, CELL), interpolation=cv2.INTER_NEAREST)
        lab = "+%ds parts=%d br=%d%%" % (ph // 30, m["parts"], round(100 * m["bright"]))
        cv2.putText(z, lab, (6, 20), cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 255), 2, cv2.LINE_AA)
        cells.append(z)
    cap.release()
    grid.append(cv2.hconcat(cells))
    p = os.path.join(out, "ALIGNED_%s.jpg" % tag)
    cv2.imwrite(p, grid[-1]); print("wrote", p)
cv2.imwrite(os.path.join(out, "PHASES_sheet.jpg"), cv2.vconcat(grid))
print("wrote PHASES_sheet.jpg (rows top->bottom: %s)" % ", ".join(t for t, _ in VIDEOS))

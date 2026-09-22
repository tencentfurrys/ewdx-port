#!/usr/bin/env python3
# v22 POV/maw comparison (session E).
# Owner report on v22: head/door FIXED; POV interior shows "just like 2 parts".
# This tool pins it with numbers instead of eyes:
#   - finds maw/POV moments by the dark-plate signature at the game-view center
#   - per sampled frame measures: dark plate fraction, bright fraction inside
#     the disc (visible prey parts), pink fraction, connected-part count
#   - aligns each video's longest maw run at its first frame (t0 = swallow-ish)
#   - writes labeled crops + a cross-video contact sheet to analysis/v22_pov/
# Reference geometry (boot log): surface 960x540, game 640x480 -> [120,0 720x540],
# i.e. uniform 1.125 scale, game center (320,240) -> video (480,270),
# maw plate 80x80 game px -> 90x90 video px, disc radius ~45 video px.
import cv2, os, sys, math
import numpy as np

VIDEOS = [
    ("ref",  r"C:/Users/RDP/Downloads/2026_09_22_17_00_09.mp4"),
    ("mumu1", r"C:/Users/RDP/Documents/MuMuSharedFolder/VideoRecords/EchidnaWarsDX(1).mp4"),
    ("mumu2", r"C:/Users/RDP/Documents/MuMuSharedFolder/VideoRecords/EchidnaWarsDX(2).mp4"),
]
here = os.path.dirname(os.path.abspath(__file__))
out = os.path.join(here, "v22_pov")
os.makedirs(out, exist_ok=True)

# tunables (video px, for a 960x540 capture with the logged viewport)
CX, CY = 480, 270          # game center
PLATE = 100                 # box half-size for plate darkness (plate is 45 half)
DISC = 44                   # interior disc radius (40 game px * 1.125)
SAMPLE_STEP = 15            # ~2 fps at 30 fps capture

def game_rect_detect(cap, W, H):
    # detect letterbox: median frame, drop near-black full rows/cols
    n = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    idxs = np.linspace(0, max(0, n - 1), 25).astype(int)
    med = None
    for i in idxs:
        cap.set(cv2.CAP_PROP_POS_FRAMES, int(i))
        ok, img = cap.read()
        if ok:
            g = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
            med = g if med is None else med + g
    if med is None:
        return CX, CY
    med = (med / len(idxs)).astype(np.uint8)
    col_ok = med.mean(axis=0) > 12
    row_ok = med.mean(axis=1) > 12
    xs = np.where(col_ok)[0]; ys = np.where(row_ok)[0]
    if len(xs) == 0 or len(ys) == 0:
        return CX, CY
    cx = (xs[0] + xs[-1]) // 2
    cy = (ys[0] + ys[-1]) // 2
    return cx, cy

def metrics(img, cx, cy):
    g = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    hsv = cv2.cvtColor(img, cv2.COLOR_BGR2HSV)
    x0, x1 = max(0, cx - PLATE), min(img.shape[1], cx + PLATE)
    y0, y1 = max(0, cy - PLATE), min(img.shape[0], cy + PLATE)
    box = g[y0:y1, x0:x1]
    dark = float((box < 60).mean())
    yy, xx = np.ogrid[:img.shape[0], :img.shape[1]]
    discmask = ((xx - cx) ** 2 + (yy - cy) ** 2) <= DISC * DISC
    V = hsv[:, :, 2]
    S = hsv[:, :, 1]
    B, G, R = img[:, :, 0].astype(int), img[:, :, 1].astype(int), img[:, :, 2].astype(int)
    bright = discmask & (V > 110)
    pink = discmask & (V > 80) & (R > 90) & (R > B + 15) & (R > G + 15)
    # part count: connected components of the bright mask inside the disc
    m = (bright * 255).astype(np.uint8)
    m = cv2.morphologyEx(m, cv2.MORPH_OPEN, np.ones((3, 3), np.uint8))
    nlab, lab, stats, _ = cv2.connectedComponentsWithStats(m, 8)
    parts = sum(1 for i in range(1, nlab) if stats[i, cv2.CC_STAT_AREA] >= 14)
    return dict(dark=dark,
                bright=float(bright.mean()), pink=float(pink.mean()),
                parts=parts, sat_mean=float(S[discmask].mean()))

def crop_zoom(img, cx, cy, half=95, zoom=2):
    x0, x1 = max(0, cx - half), min(img.shape[1], cx + half)
    y0, y1 = max(0, cy - half), min(img.shape[0], cy + half)
    c = img[y0:y1, x0:x1]
    return cv2.resize(c, (c.shape[1] * zoom, c.shape[0] * zoom),
                      interpolation=cv2.INTER_NEAREST)

def label(img, text):
    cv2.putText(img, text, (8, 22), cv2.FONT_HERSHEY_SIMPLEX, 0.55,
                (0, 255, 255), 2, cv2.LINE_AA)
    return img

summary = {}
for tag, path in VIDEOS:
    cap = cv2.VideoCapture(path)
    if not cap.isOpened():
        print("!! cannot open", path); continue
    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    W = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH)); H = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    n = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    print("== %s: %s  %dx%d @%.2ffps %.1fs (%d frames)" % (
        tag, os.path.basename(path), W, H, fps, n / fps, n))
    cx, cy = game_rect_detect(cap, W, H)
    print("   game-view center: (%d,%d)" % (cx, cy))
    cap.set(cv2.CAP_PROP_POS_FRAMES, 0)  # detect seeked near EOF; rewind
    rows = []
    fidx = 0
    while True:
        ok, img = cap.read()
        if not ok:
            break
        if fidx % SAMPLE_STEP == 0:
            m = metrics(img, cx, cy)
            m["f"] = fidx; m["t"] = fidx / fps
            rows.append(m)
        fidx += 1
    cap.release()
    # maw-present = dark plate signature
    for m in rows:
        m["maw"] = m["dark"] > 0.35
    # longest contiguous run (in samples; 1 sample = SAMPLE_STEP/fps s)
    best = (0, None, None)  # length, start_idx, end_idx
    run = 0; start = None
    for i, m in enumerate(rows):
        if m["maw"]:
            if run == 0: start = i
            run += 1
            if run > best[0]: best = (run, start, i)
        else:
            run = 0
    print("   maw samples: %d/%d | longest run %d samples (~%.1fs)" % (
        sum(1 for m in rows if m["maw"]), len(rows), best[0],
        best[0] * SAMPLE_STEP / fps))
    summary[tag] = dict(rows=rows, best=best, fps=fps, cx=cx, cy=cy, path=path)
    # timeline print of the maw window (and 4 samples before)
    if best[1] is not None:
        lo = max(0, best[1] - 2); hi = min(len(rows) - 1, best[2] + 4)
        for m in rows[lo:hi + 1]:
            print("     t=%6.1fs dark=%.2f bright=%.3f pink=%.3f parts=%d sat=%.0f%s" % (
                m["t"], m["dark"], m["bright"], m["pink"], m["parts"], m["sat_mean"],
                "  <== maw" if m["maw"] else ""))
        # crops across the run
        picks = [rows[i] for i in range(best[1], best[2] + 1)]
        step = max(1, len(picks) // 10)
        cap = cv2.VideoCapture(path)
        sheets = []
        for m in picks[::step]:
            cap.set(cv2.CAP_PROP_POS_FRAMES, m["f"])
            ok, img = cap.read()
            if not ok: continue
            z = label(crop_zoom(img, cx, cy),
                      "%s t=%.1fs bright=%d%% parts=%d" % (tag, m["t"], 100 * m["bright"], m["parts"]))
            p = os.path.join(out, "%s_t%05.1f.jpg" % (tag, m["t"]))
            cv2.imwrite(p, z)
            sheets.append(z)
        cap.release()
        if sheets:
            hmin = min(s.shape[0] for s in sheets)
            sheets = [s[:hmin] for s in sheets]
            cv2.imwrite(os.path.join(out, "%s_sheet.jpg" % tag), cv2.hconcat(sheets))
            print("   sheet:", "%s_sheet.jpg (%d crops)" % (tag, len(sheets)))

# cross-video alignment sheet: each video's longest run, t0+0 .. t0+N
cols_per = 6
cell = 320
grid_rows = []
for tag, s in summary.items():
    if s["best"][1] is None: continue
    run_rows = list(range(s["best"][1], s["best"][2] + 1))
    step = max(1, len(run_rows) // cols_per)
    picks = [s["rows"][i] for i in run_rows[::step]][:cols_per]
    cap = cv2.VideoCapture(s["path"])
    cells = []
    for m in picks:
        cap.set(cv2.CAP_PROP_POS_FRAMES, m["f"])
        ok, img = cap.read()
        if not ok: continue
        z = label(crop_zoom(img, s["cx"], s["cy"], half=95, zoom=2),
                  "t0+%.1fs parts=%d" % (m["t"] - s["rows"][s["best"][1]]["t"], m["parts"]))
        z = cv2.resize(z, (cell, cell))
        cells.append(z)
    cap.release()
    while len(cells) < cols_per:
        cells.append(np.zeros((cell, cell, 3), np.uint8))
    grid_rows.append(cv2.hconcat(cells))
if grid_rows:
    wmin = min(r.shape[1] for r in grid_rows)
    grid_rows = [r[:, :wmin] for r in grid_rows]
    p = os.path.join(out, "ALIGN_sheet.jpg")
    cv2.imwrite(p, cv2.vconcat(grid_rows))
    print("alignment sheet:", p)
print("done ->", out)

# v26: per-angle ring ridge + red extent + black-plate thickness, referenced to
# the red-cluster centroid. Resolves where the ring actually is and how fat the
# black plate is per direction (PC vs Android).
import cv2, numpy as np, os

BASE = os.path.expanduser("~/Downloads/incoming")

def crop_game(img):
    h, w = img.shape[:2]
    return img[40:h-40, 107:w-107]

def report(path, label):
    img = cv2.imread(path)
    c = crop_game(img)
    b, g, r = c[:,:,0].astype(np.int32), c[:,:,1].astype(np.int32), c[:,:,2].astype(np.int32)
    m = ((r > 90) & (r > 1.25*g) & (r > 1.25*b)).astype(np.uint8)
    n, lab, stats, cent = cv2.connectedComponentsWithStats(m, 8)
    if n <= 1 or stats[1:, cv2.CC_STAT_AREA].max() < 800:
        print(f"{label}: no disc"); return
    j = 1 + int(np.argmax(stats[1:, cv2.CC_STAT_AREA]))
    cx, cy = cent[j]
    NA = 36
    rings, reds, plates = [], [], []
    for a in range(NA):
        th = 2*np.pi*a/NA
        dx, dy = np.cos(th), np.sin(th)
        lums, reds_a, rgbs = [], [], []
        for rr in range(4, 200):
            x, y = int(round(cx + rr*dx)), int(round(cy + rr*dy))
            if not (0 <= x < c.shape[1] and 0 <= y < c.shape[0]):
                break
            px = c[y, x]
            lum = (int(px[2])*2 + int(px[1])*3 + int(px[0])) // 6
            lums.append(lum)
            reds_a.append(1 if (px[2] > 90 and px[2] > 1.25*px[1] and px[2] > 1.25*px[0]) else 0)
            rgbs.append((int(px[2]), int(px[1]), int(px[0])))
        if len(lums) < 60:
            continue
        lums = np.array(lums); reds_a = np.array(reds_a)
        # ring ridge: outermost strong local max of luminance in r<130
        best_r, best_l = 0, -1
        for i in range(6, min(130, len(lums)-3)):
            if lums[i] >= lums[i-2:i+3].max() and lums[i] > best_l:
                # require it to be a local peak
                if lums[i] >= lums[i-1] and lums[i] >= lums[i+1]:
                    best_r, best_l = i+4, lums[i]
        rings.append(best_r)
        # red extent: furthest red pixel along the ray
        idx = np.where(reds_a)[0]
        reds.append((idx.max()+4) if len(idx) else 0)
        # plate: black band (lum<25) run length in r in [red_extent+3 .. ring+30]
        p0 = (reds[-1]+3) if reds[-1] else 4
        run = 0; maxrun = 0
        for rr in range(p0, min(len(lums), best_r+30)):
            if lums[rr-4] < 25:
                run += 1; maxrun = max(maxrun, run)
            else:
                run = 0
        plates.append(maxrun)
    rings = np.array(rings); reds = np.array(reds); plates = np.array(plates)
    print(f"===== {label} center=({cx:.0f},{cy:.0f}) =====")
    print(f"  ring ridge r: med={np.median(rings):.0f} min={rings.min()} max={rings.max()} per-angle={list(rings)}")
    print(f"  red extent r: med={np.median(reds):.0f} min={reds.min()} max={reds.max()}")
    print(f"  red/ring ratio: med={np.median(reds)/np.median(rings):.2f}")
    print(f"  black-plate run: med={np.median(plates):.0f} max={plates.max()} per-angle={list(plates)}")

for t in ["124.5", "130.5", "146.5"]:
    report(os.path.join(BASE, f"png/pc/t{t}.png"), f"PC t={t}")
for t in ["71.0", "75.0", "79.0", "83.0"]:
    report(os.path.join(BASE, f"png/and/t{t}.png"), f"ANDROID t={t}")

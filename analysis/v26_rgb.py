# v26: RGB sampling around the porthole disc on PC vs Android POV frames.
# Distinguishes "black plate" (5,5,5) from "night scene" (45,45,45) and measures
# ring/interior radii from the detected disc center.
import cv2, numpy as np, os

BASE = os.path.expanduser("~/Downloads/incoming")

def crop_game(img):
    h, w = img.shape[:2]
    return img[40:h-40, 107:w-107]

def find_disc(c):
    # find the ring: strong red cluster -> center guess; refine via dark ring search
    b, g, r = c[:,:,0].astype(np.int32), c[:,:,1].astype(np.int32), c[:,:,2].astype(np.int32)
    m = ((r > 90) & (r > 1.25*g) & (r > 1.25*b)).astype(np.uint8)
    n, lab, stats, cent = cv2.connectedComponentsWithStats(m, 8)
    if n <= 1:
        return None
    j = 1 + int(np.argmax(stats[1:, cv2.CC_STAT_AREA]))
    if stats[j, cv2.CC_STAT_AREA] < 800:
        return None
    return cent[j], stats[j], m

def radial(c, cx, cy, rmax, nang=72):
    # mean RGB per radius bin
    out = []
    for rr in range(0, rmax, 2):
        vals = []
        for a in range(nang):
            th = 2*np.pi*a/nang
            x = int(round(cx + rr*np.cos(th))); y = int(round(cy + rr*np.sin(th)))
            if 0 <= x < c.shape[1] and 0 <= y < c.shape[0]:
                vals.append(c[y, x].tolist())
        v = np.array(vals)
        out.append((rr, v[:,2].mean(), v[:,1].mean(), v[:,0].mean()))
    return out

def corner_samples(c, cx, cy, rad):
    cx, cy, rad = int(cx), int(cy), int(rad)
    # 4 diagonal corners at radius*1.0 of the disc's bounding box (like game 80x80 box corners)
    d = int(rad * 0.85)
    pts = {"TL": (cx-d, cy-d), "TR": (cx+d, cy-d), "BL": (cx-d, cy+d), "BR": (cx+d, cy+d),
           "L":  (cx-int(rad*1.15), cy), "R": (cx+int(rad*1.15), cy),
           "T":  (cx, cy-int(rad*1.15)), "B": (cx, cy+int(rad*1.15))}
    res = {}
    for k, (x, y) in pts.items():
        if 0 <= x < c.shape[1]-3 and 0 <= y < c.shape[0]-3:
            p = c[max(0,y-3):y+3, max(0,x-3):x+3].reshape(-1,3).mean(0)
            res[k] = (int(p[2]), int(p[1]), int(p[0]))  # RGB
    return res

def report(path, label):
    img = cv2.imread(path)
    c = crop_game(img)
    fd = find_disc(c)
    if not fd:
        print(f"{label}: no disc"); return
    (cx, cy), st, m = fd
    area = int(st[cv2.CC_STAT_AREA])
    w = int(st[cv2.CC_STAT_WIDTH]); h = int(st[cv2.CC_STAT_HEIGHT])
    rad = int(max(w, h) / 2 * 1.15)
    print(f"===== {label} center=({cx:.0f},{cy:.0f}) redcluster={w}x{h} area={area} =====")
    prof = radial(c, cx, cy, rad + 30)
    line = []
    for rr, r_, g_, b_ in prof:
        line.append(f"r{rr}:({r_:.0f},{g_:.0f},{b_:.0f})")
    print("  radial RGB: " + " ".join(line))
    corners = corner_samples(c, cx, cy, rad)
    print("  around-disc RGB: " + " ".join(f"{k}={v}" for k, v in corners.items()))

# PC POV moments
for t in ["124.5", "130.5", "146.5"]:
    report(os.path.join(BASE, f"png/pc/t{t}.png"), f"PC t={t}")
# Android POV moments
for t in ["71.0", "75.0", "79.0", "83.0"]:
    report(os.path.join(BASE, f"png/and/t{t}.png"), f"ANDROID t={t}")

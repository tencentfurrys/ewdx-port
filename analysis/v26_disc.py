# v26: locate the POV porthole disc in each video via red-pixel clustering,
# then sample the bounding-square corners (the corner-black bug signature).
import cv2, numpy as np, os

BASE = os.path.expanduser("~/Downloads/incoming")

def red_mask(c):
    b, g, r = c[:,:,0].astype(np.int32), c[:,:,1].astype(np.int32), c[:,:,2].astype(np.int32)
    return ((r > 90) & (r > 1.25*g) & (r > 1.25*b)).astype(np.uint8)

def frames(d):
    return sorted(f for f in os.listdir(d) if f.endswith(".jpg"))

def disc_stats(img):
    h, w = img.shape[:2]
    c = img[40:h-40, 107:w-107]
    m = red_mask(c)
    if m.sum() < 300:
        return None
    n, lab, stats, cent = cv2.connectedComponentsWithStats(m, 8)
    if n <= 1:
        return None
    j = 1 + int(np.argmax(stats[1:, cv2.CC_STAT_AREA]))
    x, y, ww, hh, area = stats[j]
    if area < 500:
        return None
    # bounding square of the cluster (the disc's 80x80 game box)
    cx, cy = int(cent[j][0]), int(cent[j][1])
    rad = max(ww, hh) // 2
    x0, y0 = max(0, cx - rad), max(0, cy - rad)
    x1, y1 = min(c.shape[1], cx + rad), min(c.shape[0], cy + rad)
    # corner patches (8x8) of that bounding square + center patch + ring sample
    def patch(px, py):
        p = c[max(0,py-4):py+4, max(0,px-4):px+4]
        return tuple(int(v) for v in p.reshape(-1,3).mean(0))
    corners = [patch(x0+6,y0+6), patch(x1-6,y0+6), patch(x0+6,y1-6), patch(x1-6,y1-6)]
    center = patch(cx, cy)
    edge = patch((x0+x1)//2, y0+4)
    # red coverage inside the bounding square (interior fill health)
    box = m[y0:y1, x0:x1]
    redcov = float(box.mean())
    return dict(t=None, box=(x0,y0,x1-x0,y1-y0), area=int(area), redcov=redcov,
                corners=corners, center=center, edge=edge)

def scan(d, label, want=None):
    out = []
    for i, f in enumerate(frames(d)):
        img = cv2.imread(os.path.join(d, f))
        s = disc_stats(img)
        if s:
            s["t"] = i/2.0
            out.append(s)
    print(f"===== {label}: {len(out)} frames with a red cluster =====")
    # summarize timeline: 1 per second-ish (every 2nd frame)
    for s in out[::4]:
        cs = " ".join(f"({r:3d},{g:3d},{b:3d})" for r,g,b in s["corners"])
        print(f"  t={s['t']:6.1f}s box={s['box']} area={s['area']:6d} redcov={s['redcov']*100:5.1f}% corners={cs}")
    return out

pc = scan(os.path.join(BASE, "frames/pc"), "PC")
an = scan(os.path.join(BASE, "frames/and"), "ANDROID")

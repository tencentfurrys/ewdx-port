# v26: ring-relative annulus analysis. Auto-detect the bright ring ridge radius,
# then histogram luminance just OUTSIDE the ring (where the mask plate would show)
# all-angle and in the 4 corner cones. PC vs Android.
import cv2, numpy as np, os

BASE = os.path.expanduser("~/Downloads/incoming")

def crop_game(img):
    h, w = img.shape[:2]
    return img[40:h-40, 107:w-107]

def ring_radius(c, cx, cy, rmax=140):
    # ring = local max of mean luminance over radius, in the outer half
    lums = []
    for rr in range(10, rmax, 2):
        vals = []
        for a in range(90):
            th = 2*np.pi*a/90
            x = int(round(cx + rr*np.cos(th))); y = int(round(cy + rr*np.sin(th)))
            if 0 <= x < c.shape[1] and 0 <= y < c.shape[0]:
                px = c[y, x]
                vals.append((int(px[2])*2 + int(px[1])*3 + int(px[0])) // 6)
        lums.append((rr, np.mean(vals)))
    arr = np.array(lums)
    # smooth then find the strongest local max in r in [30, rmax]
    sm = np.convolve(arr[:,1], np.ones(3)/3, mode="same")
    lo, hi = 30, len(arr)-2
    seg = sm[lo:hi]
    j = int(np.argmax(seg)) + lo
    return int(arr[j,0]), arr[j,1]

def annulus_stats(c, cx, cy, r0, r1, ang_lo=0, ang_hi=360):
    vals = []
    for rr in range(int(r0), int(r1)):
        for a in range(ang_lo, ang_hi, 2):
            th = 2*np.pi*a/360
            x = int(round(cx + rr*np.cos(th))); y = int(round(cy + rr*np.sin(th)))
            if 0 <= x < c.shape[1] and 0 <= y < c.shape[0]:
                px = c[y, x]
                vals.append((int(px[2])*2 + int(px[1])*3 + int(px[0])) // 6)
    if not vals:
        return None
    v = np.array(vals)
    return dict(mean=float(v.mean()), p5=float(np.percentile(v,5)), p50=float(np.percentile(v,50)),
                blackfrac=float((v < 30).mean()), n=len(v))

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
    R, rl = ring_radius(c, cx, cy)
    band = annulus_stats(c, cx, cy, R+4, R+18)
    corners = []
    for lo, hi, nm in [(30,60,"TL"),(120,150,"TR"),(210,240,"BL"),(300,330,"BR")]:
        corners.append((nm, annulus_stats(c, cx, cy, R+8, R+28, lo, hi)))
    inner = annulus_stats(c, cx, cy, 6, 20)
    print(f"===== {label} center=({cx:.0f},{cy:.0f}) ringR={R}(lum{rl:.0f}) =====")
    print(f"  inner r6-20: mean={inner['mean']:.0f} p50={inner['p50']:.0f}")
    print(f"  band R+4..R+18 all-angle: mean={band['mean']:.0f} p5={band['p5']:.0f} p50={band['p50']:.0f} black<30={band['blackfrac']*100:.0f}%")
    for nm, s in corners:
        if s:
            print(f"  corner {nm} R+8..R+28: mean={s['mean']:.0f} p5={s['p5']:.0f} black<30={s['blackfrac']*100:.0f}%")

for t in ["124.5", "130.5", "146.5"]:
    report(os.path.join(BASE, f"png/pc/t{t}.png"), f"PC t={t}")
for t in ["71.0", "75.0", "79.0", "83.0"]:
    report(os.path.join(BASE, f"png/and/t{t}.png"), f"ANDROID t={t}")

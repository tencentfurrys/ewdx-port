# v26: fine-grained scan for the porthole/maw disc in both videos.
# The porthole = large dark disc with interior content. Per frame: measure the
# largest connected dark component inside the central game box + red richness.
import cv2, numpy as np, os

BASE = os.path.expanduser("~/Downloads/incoming")

def analyze(d, label):
    files = sorted(f for f in os.listdir(d) if f.endswith(".jpg"))
    print(f"===== {label} ({len(files)} frames @2fps) =====")
    for i, f in enumerate(files):
        img = cv2.imread(os.path.join(d, f))
        h, w = img.shape[:2]
        c = img[40:h-40, 107:w-107]
        b, g, r = c[:,:,0].astype(np.int32), c[:,:,1].astype(np.int32), c[:,:,2].astype(np.int32)
        lum = ((r*2 + g*3 + b) // 6).astype(np.uint8)
        dark = (lum < 55).astype(np.uint8)
        n, lab, stats, _ = cv2.connectedComponentsWithStats(dark, 8)
        if n > 1:
            areas = stats[1:, cv2.CC_STAT_AREA]
            j = 1 + int(np.argmax(areas))
            x, y, ww, hh, area = stats[j]
            frac = area / dark.size
        else:
            x = y = ww = hh = area = 0; frac = 0.0
        red = float(((r > 100) & (r > 1.3*g) & (r > 1.3*b)).mean())
        # red richness inside the biggest dark blob's bounding box
        if area > 0:
            box = c[max(0,y):y+hh, max(0,x):x+ww]
            rb, rg, rr = box[:,:,0].astype(np.int32), box[:,:,1].astype(np.int32), box[:,:,2].astype(np.int32)
            boxred = float(((rr > 90) & (rr > 1.25*rg) & (rr > 1.25*rb)).mean())
        else:
            boxred = 0.0
        if frac > 0.10 or red > 0.03 or i % 10 == 0:
            print(f"  t={i/2:6.1f}s f{i:03d} darkblob={frac*100:5.1f}% box=({x},{y} {ww}x{hh}) red%={red*100:5.1f} blobred%={boxred*100:5.1f}")

analyze(os.path.join(BASE, "frames/pc"), "PC")
analyze(os.path.join(BASE, "frames/and"), "ANDROID")

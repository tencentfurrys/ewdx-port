# v26: Hough-circle detection of the porthole ring, then normalized interior
# composition (red / dark-red / black / bright) inside the disc, PC vs Android.
import cv2, numpy as np, os

BASE = os.path.expanduser("~/Downloads/incoming")

def crop_game(img):
    h, w = img.shape[:2]
    return img[40:h-40, 107:w-107]

def classify(c, cx, cy, R):
    # sample dense disc grid
    red = darkred = black = bright = other = 0
    n = 0
    for yy in range(int(cy-R), int(cy+R), 2):
        for xx in range(int(cx-R), int(cx+R), 2):
            if (xx-cx)**2 + (yy-cy)**2 > R*R:
                continue
            if not (0 <= xx < c.shape[1] and 0 <= yy < c.shape[0]):
                continue
            px = c[yy, xx]
            r, g, b = int(px[2]), int(px[1]), int(px[0])
            lum = (r*2 + g*3 + b) // 6
            n += 1
            if r > 90 and r > 1.25*g and r > 1.25*b:
                red += 1
            elif r > 60 and r > 1.15*g and r > 1.15*b:
                darkred += 1
            elif lum < 28:
                black += 1
            elif lum > 170:
                bright += 1
            else:
                other += 1
    return red/n, darkred/n, black/n, bright/n, other/n

def report(path, label):
    img = cv2.imread(path)
    c = crop_game(img)
    g = cv2.cvtColor(c, cv2.COLOR_BGR2GRAY)
    g = cv2.medianBlur(g, 5)
    circles = cv2.HoughCircles(g, cv2.HOUGH_GRADIENT, dp=1.2, minDist=120,
                               param1=110, param2=52, minRadius=45, maxRadius=130)
    print(f"===== {label} =====")
    if circles is None:
        print("  no circle found"); return
    best = None
    for x, y, R in circles[0][:4]:
        # score: red content inside must be meaningful
        rf, dr, bf, brf, ot = classify(c, x, y, R)
        score = rf + dr*0.5
        if best is None or score > best[0]:
            best = (score, x, y, R, rf, dr, bf, brf, ot)
    _, x, y, R, rf, dr, bf, brf, ot = best
    print(f"  disc center=({x:.0f},{y:.0f}) R={R:.0f}px  diameter={2*R:.0f}px (video px; game 640x400 shown 1:1)")
    print(f"  interior: red={rf*100:.0f}% darkred={dr*100:.0f}% black={bf*100:.0f}% bright={brf*100:.0f}% mid={ot*100:.0f}%")
    # corner zones of the disc's bounding square (the 80x80 box corners)
    d = int(R*0.92)
    for nm, (xx, yy) in {"TL": (int(x-d), int(y-d)), "TR": (int(x+d), int(y-d)), "BL": (int(x-d), int(y+d)), "BR": (int(x+d), int(y+d))}.items():
        p = c[max(0,yy-6):yy+6, max(0,xx-6):xx+6].reshape(-1,3).mean(0)
        print(f"    corner {nm}: RGB=({p[2]:.0f},{p[1]:.0f},{p[0]:.0f})")

for t in ["124.5", "130.5", "140.0", "146.5", "152.0"]:
    report(os.path.join(BASE, f"png/pc/t{t}.png"), f"PC t={t}")
for t in ["71.0", "75.0", "79.0", "83.0", "87.0"]:
    report(os.path.join(BASE, f"png/and/t{t}.png"), f"ANDROID t={t}")

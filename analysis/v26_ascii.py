# v26: ASCII-render the porthole disc regions from both videos for numeric
# visual comparison + measure ring/plate geometry.
import cv2, numpy as np, os

BASE = os.path.expanduser("~/Downloads/incoming")
RAMP = " .:-=+*#%@"

def load(d, t, fps=2):
    i = int(round(t * fps))
    p = os.path.join(d, "f%04d.jpg" % i)
    return cv2.imread(p) if os.path.exists(p) else None

def crop_game(img):
    h, w = img.shape[:2]
    return img[40:h-40, 107:w-107]

def render(img, x, y, w, h, cols=76):
    c = crop_game(img)
    x = max(0, x); y = max(0, y)
    w = min(w, c.shape[1]-x); h = min(h, c.shape[0]-y)
    reg = c[y:y+h, x:x+w]
    rows = max(1, int(cols * h / w * 0.5))
    small = cv2.resize(reg, (cols, rows), interpolation=cv2.INTER_AREA)
    out = []
    for row in small:
        line = ""
        for px in row:
            b, g, r = int(px[0]), int(px[1]), int(px[2])
            lum = (r*2 + g*3 + b) // 6
            if r > 90 and r > 1.25*g and r > 1.25*b:
                ch = "R" if lum > 60 else "r"
            elif lum < 30:
                ch = "#"
            elif lum < 60:
                ch = "%"
            else:
                ch = RAMP[min(9, lum * 10 // 256)]
            line += ch
        out.append(line)
    return "\n".join(out)

def banner(t):
    print(f"\n---------- t={t}s ----------")

print("################ PC t=124.5s (porthole w/ red) ################")
img = load(os.path.join(BASE, "frames/pc"), 124.5)
print(render(img, 250, 60, 260, 260))

print("\n################ PC t=130.5s ################")
img = load(os.path.join(BASE, "frames/pc"), 130.5)
print(render(img, 250, 60, 260, 260))

print("\n################ PC t=146.5s ################")
img = load(os.path.join(BASE, "frames/pc"), 146.5)
print(render(img, 370, 100, 240, 240))

print("\n################ ANDROID t=75.0s (largest red disc) ################")
img = load(os.path.join(BASE, "frames/and"), 75.0)
print(render(img, 160, 140, 260, 260))

print("\n################ ANDROID t=79.0s ################")
img = load(os.path.join(BASE, "frames/and"), 79.0)
print(render(img, 160, 140, 260, 260))

print("\n################ ANDROID t=71.0s ################")
img = load(os.path.join(BASE, "frames/and"), 71.0)
print(render(img, 100, 140, 260, 260))

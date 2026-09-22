#!/usr/bin/env python3
# v21 ground truth: crop the maw region from several captured frames and save a
# zoomed contact sheet so the reference behavior is visually pinned. The maw is
# an 80x80 logical quad on a 480x854 (portrait, letterboxed 640x480 subview)
# screen recording of the REFERENCE game (2026_09_19 capture predates v20).
import cv2, os, numpy as np, sys

here = os.path.dirname(os.path.abspath(__file__))
frames_dir = os.path.join(here, "frames_2026-09-21")
out = []

# Try to re-extract dense frames straight from the capture video
vid = r"C:/Users/runneradmin/Downloads/2026_09_19_23_36_54.mp4"
dense_dir = os.path.join(here, "v21_dense")
os.makedirs(dense_dir, exist_ok=True)
cap = cv2.VideoCapture(vid)
if cap.isOpened():
    fps = cap.get(cv2.CAP_PROP_FPS)
    n = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    print("video: %dx%d @ %.2f fps, %d frames" % (
        cap.get(cv2.CAP_PROP_FRAME_WIDTH), cap.get(cv2.CAP_PROP_FRAME_HEIGHT), fps, n))
    # sample 1 fps for the first 30s -> find maw-visible frames by darkness signature
    picks = []
    fidx = 0
    while True:
        ok, img = cap.read()
        if not ok:
            break
        if fidx % 30 == 0:  # ~1 fps at 30fps capture
            picks.append((fidx, img))
        fidx += 1
    cap.release()
    print("sampled %d frames" % len(picks))
    # score each: darkness of center region (maw = large near-black disc region)
    scored = []
    for fidx, img in picks:
        h, w = img.shape[:2]
        cx, cy = w // 2, h // 2
        reg = cv2.cvtColor(img[max(0, cy-140):cy+140, max(0, cx-140):cx+140], cv2.COLOR_BGR2GRAY)
        dark = (reg < 60).mean()
        scored.append((dark, fidx, img))
    scored.sort(key=lambda t: -t[0])
    for rank, (dark, fidx, img) in enumerate(scored[:12]):
        h, w = img.shape[:2]
        # crop wide region around center (the maw can be offset while grabbing)
        cx, cy = w // 2, h // 2
        crop = img[max(0, cy-170):min(h, cy+170), max(0, cx-170):min(w, cx+170)]
        zoom = cv2.resize(crop, (510, 510), interpolation=cv2.INTER_NEAREST)
        cv2.putText(zoom, "t=%ds dark=%.2f" % (fidx // 30, dark), (8, 24),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 255), 2)
        p = os.path.join(dense_dir, "maw_%02d.jpg" % rank)
        cv2.imwrite(p, zoom)
        out.append(p)
        print("  rank %d: t=%.1fs dark %.2f -> %s" % (rank, fidx / 30.0, dark, os.path.basename(p)))
else:
    sys.exit("could not open video: " + vid)

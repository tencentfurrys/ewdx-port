#!/usr/bin/env python3
# v21 device captures: extract evenly spaced frames from each MuMu recording
# for visual triage (head detached, floating door, POV weird colors).
import cv2, os

out_root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "v21_mumu")
os.makedirs(out_root, exist_ok=True)
src = r"C:/Users/runneradmin/Documents/MuMuSharedFolder/VideoRecords"
vids = ["EchidnaWarsDX(5).mp4", "EchidnaWarsDX(6).mp4",
        "EchidnaWarsDX(7).mp4", "EchidnaWarsDX(8).mp4"]
for v in vids:
    p = os.path.join(src, v)
    cap = cv2.VideoCapture(p)
    if not cap.isOpened():
        print("SKIP", v)
        continue
    fps = cap.get(cv2.CAP_PROP_FPS)
    n = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    tag = v.replace("EchidnaWarsDX", "vid").replace(".mp4", "")
    d = os.path.join(out_root, tag)
    os.makedirs(d, exist_ok=True)
    print("%s: %dx%d @ %.1ffps, %.1fs (%d frames)" % (v,
          cap.get(cv2.CAP_PROP_FRAME_WIDTH), cap.get(cv2.CAP_PROP_FRAME_HEIGHT),
          fps, n / fps if fps else 0, n))
    k = 12
    for i in range(k):
        fidx = int(n * i / k)
        cap.set(cv2.CAP_PROP_POS_FRAMES, fidx)
        ok, img = cap.read()
        if not ok:
            continue
        t = fidx / fps if fps else 0
        cv2.putText(img, "%s t=%.1fs" % (tag, t), (10, 28),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 255, 255), 2)
        cv2.imwrite(os.path.join(d, "f%02d_t%.1f.jpg" % (i, t)), img)
    cap.release()
print("done ->", out_root)

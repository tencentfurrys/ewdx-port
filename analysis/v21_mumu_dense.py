#!/usr/bin/env python3
# Dense crops around the reported moments: head-oversize (vid8 ~39s) and a
# sweep of vid5 (longest run) + vid6/7 for door/POV anomalies.
import cv2, os

root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "v21_mumu")
src = r"C:/Users/runneradmin/Documents/MuMuSharedFolder/VideoRecords"
jobs = [
    ("EchidnaWarsDX(8).mp4", "vid8_head", [33, 35, 37, 39, 41, 43, 45, 47]),
    ("EchidnaWarsDX(5).mp4", "vid5_sweep", [20, 50, 80, 110, 140, 170]),
    ("EchidnaWarsDX(6).mp4", "vid6_sweep", [10, 40, 70, 100, 125]),
    ("EchidnaWarsDX(7).mp4", "vid7_sweep", [8, 20, 33, 45, 50]),
]
for v, tag, times in jobs:
    cap = cv2.VideoCapture(os.path.join(src, v))
    if not cap.isOpened():
        print("SKIP", v); continue
    fps = cap.get(cv2.CAP_PROP_FPS) or 30
    d = os.path.join(root, tag); os.makedirs(d, exist_ok=True)
    for t in times:
        cap.set(cv2.CAP_PROP_POS_FRAMES, int(t * fps))
        ok, img = cap.read()
        if not ok: continue
        cv2.putText(img, "%s t=%ds" % (tag, t), (10, 28),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 255, 255), 2)
        cv2.imwrite(os.path.join(d, "t%03d.jpg" % t), img)
    cap.release()
    print(tag, "ok")

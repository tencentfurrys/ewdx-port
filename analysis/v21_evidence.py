#!/usr/bin/env python3
# v21 evidence: measure the maw "black box" and normal-scene stats from the
# committed 09-21 frames (video: 480x854 screen recording of the web reference).
import cv2, os, numpy as np

d = os.path.join(os.path.dirname(__file__), "frames_2026-09-21")
frames = sorted(f for f in os.listdir(d) if f.startswith("frame_"))

print(f"{'frame':<22}{'meanRGB(center120)':>22}{'nearblack%':>12}{'meanRGB(full)':>16}")
for f in frames:
    img = cv2.imread(os.path.join(d, f))
    if img is None:
        continue
    h, w = img.shape[:2]
    cx, cy = w // 2, h // 2
    # the maw 80x80 logical quad at 480-wide upscale ~ 120..150 px; probe center + upper-center
    for (name, ox, oy, s) in (("center", cx, cy, 60), ("upcenter", cx, cy - 90, 60)):
        reg = img[oy - s:oy + s, ox - s:ox + s]
        mean = reg.reshape(-1, 3).mean(axis=0)[::-1]  # BGR->RGB
        gray = cv2.cvtColor(reg, cv2.COLOR_BGR2GRAY)
        nb = (gray < 40).mean() * 100
        print(f"{f:<22}{name:>8} {mean[0]:6.1f},{mean[1]:6.1f},{mean[2]:6.1f}{nb:9.1f}%")
    full = cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    print(f"{f:<22} full-mean {full.mean():6.1f}  full-nearblack {(full < 40).mean()*100:5.1f}%")

import cv2, numpy as np

def measure(img, ycenter_hint, xrange, label):
    # find disc center row: row with the widest bright (silver ring) span
    best=(0,0)
    for y in range(ycenter_hint[0], ycenter_hint[1], 2):
        row = img[y, xrange[0]:xrange[1]]
        bright = (row.mean(axis=1) > 150)
        n = bright.sum()
        if n > best[1]: best=(y,n)
    y = best[0]
    row = img[y].astype(int)
    m = row.mean(axis=1)
    bright = m > 140
    pure = m < 14
    # plate: longest pure-black run; ring: bright pixels inside that run's neighborhood
    runs=[]; i=0
    while i < len(m):
        j=i
        while j<len(m) and pure[j]==pure[i]: j+=1
        if pure[i] and j-i>30: runs.append((i,j))
        i=j
    if not runs:
        print(label, 'no plate found'); return
    pa, pb = max(runs, key=lambda r: r[1]-r[0])
    # ring: bright pixels within plate +- margin
    xs = [x for x in range(max(0,pa-15), min(len(m),pb+15)) if bright[x]]
    if xs:
        ra, rb = min(xs), max(xs)
        print(f'{label}: y={y} plate {pa}..{pb} (W={pb-pa})  ring {ra}..{rb} (D={rb-ra})  cornerL={ra-pa} cornerR={pb-rb}')
    else:
        print(f'{label}: y={y} plate W={pb-pa}, no ring pixels found')
    # draw markers and save crop
    vis = img.copy()
    cv2.line(vis, (pa, y-30), (pa, y+30), (0,0,255), 2)
    cv2.line(vis, (pb, y-30), (pb, y+30), (0,0,255), 2)
    if xs:
        cv2.line(vis, (ra, y-40), (ra, y+40), (0,255,0), 2)
        cv2.line(vis, (rb, y-40), (rb, y+40), (0,255,0), 2)
    cv2.imwrite(f'ewdx-port/analysis/v252_evidence/m_{label.replace(" ","_").replace(".","")}.jpg', cv2.resize(vis[y-260:y+260, max(0,pa-220):pb+220], (0,0), fx=0.75, fy=0.75), [cv2.IMWRITE_JPEG_QUALITY,82])

im1 = cv2.imread('ewdx-port/analysis/v24_blackbox/v241_owner_test.png', cv2.IMREAD_COLOR)
cap=cv2.VideoCapture('MuMuSharedFolder/VideoRecords/EchidnaWarsDX(1).mp4')
cap.set(cv2.CAP_PROP_POS_FRAMES,60); ok,f=cap.read(); cap.release()
r8 = cv2.imread('ewdx-port/analysis/v21_dense/maw_08.jpg')

measure(im1, (150, 380), (380, 900), 'v24.1')
measure(f, (150, 400), (100, 620), 'v25.1')
measure(r8, (100, 480), (0, 510), 'ref08')

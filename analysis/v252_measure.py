import cv2, numpy as np

def profile(img, y, label):
    # scan one row: find black runs and the disc (non-black) span
    row = img[y].mean(axis=1)
    blk = row < 45
    # disc = longest non-black run
    best=(0,0,0); i=0
    while i < len(blk):
        if not blk[i]:
            j=i
            while j<len(blk) and not blk[j]: j+=1
            if j-i > best[2]: best=(i,j,j-i)
            i=j
        else: i+=1
    x1,x2,_ = best
    # black run immediately left of disc and right of disc
    l0=x1
    while l0>0 and blk[l0-1]: l0-=1
    r2=x2
    while r2<len(blk)-1 and blk[r2+1]: r2+=1
    print(f'{label} y={y}: black-left {l0}..{x1} ({x1-l0}px)  disc {x1}..{x2} ({x2-x1}px)  black-right {x2}..{r2} ({r2-x2}px)')
    return (x1-l0, x2-x1, r2-x2)

# v24.1 screenshot: find disc center row by red density in right half
im1 = cv2.imread('ewdx-port/analysis/v24_blackbox/v241_owner_test.png', cv2.IMREAD_COLOR)
best=(0,0)
for y in range(60, 480):
    seg = im1[y, 400:820]
    red = ((seg[:,2]>90)&(seg[:,2]>seg[:,0]+30)).sum()
    if red>best[1]: best=(y,red)
y1 = best[0]; print('v241 disc center row', y1, 'redpx', best[1])
profile(im1, y1, 'v24.1')

# v25.1 frame 60
cap=cv2.VideoCapture('MuMuSharedFolder/VideoRecords/EchidnaWarsDX(1).mp4')
cap.set(cv2.CAP_PROP_POS_FRAMES,60); ok,f=cap.read(); cap.release()
best=(0,0)
for y in range(60, 480):
    seg = f[y, 100:520]
    red = ((seg[:,2]>90)&(seg[:,2]>seg[:,0]+30)).sum()
    if red>best[1]: best=(y,red)
y2 = best[0]; print('v25.1 disc center row', y2, 'redpx', best[1])
profile(f, y2, 'v25.1')

# also vertical profile through disc center column (top/bottom corners)
def vprofile(img, x, label):
    col = img[:,x].mean(axis=1)
    blk = col < 45
    best=(0,0,0); i=0
    while i < len(blk):
        if not blk[i]:
            j=i
            while j<len(blk) and not blk[j]: j+=1
            if j-i > best[2]: best=(i,j,j-i)
            i=j
        else: i+=1
    yy1,yy2,_=best
    t0=yy1
    while t0>0 and blk[t0-1]: t0-=1
    b2=yy2
    while b2<len(blk)-1 and blk[b2+1]: b2+=1
    print(f'{label} x={x}: black-top {t0}..{yy1} ({yy1-t0}px)  disc {yy1}..{yy2} ({yy2-yy1}px)  black-bot {yy2}..{b2} ({b2-yy2}px)')

xc1=(profile(im1,y1,'v24.1-h')[0]+0,)  # placeholder

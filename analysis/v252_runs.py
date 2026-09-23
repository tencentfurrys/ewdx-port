import cv2, numpy as np

def runs(img, y, x0, x1, label):
    row = img[y, x0:x1].mean(axis=1)
    blk = row < 45
    out=[]; i=0
    while i < len(blk):
        j=i
        while j<len(blk) and blk[j]==blk[i]: j+=1
        out.append(('BLK' if blk[i] else 'vis', x0+i, x0+j, j-i))
        i=j
    print(f'--- {label} row y={y}')
    for k,(t,a,b,w) in enumerate(out):
        if w>=3: print(f'  {t} {a}..{b} ({w}px)')

def vruns(img, x, y0, y1, label):
    col = img[y0:y1, x].mean(axis=1)
    blk = col < 45
    out=[]; i=0
    while i < len(blk):
        j=i
        while j<len(blk) and blk[j]==blk[i]: j+=1
        out.append(('BLK' if blk[i] else 'vis', y0+i, y0+j, j-i))
        i=j
    print(f'--- {label} col x={x}')
    for t,a,b,w in out:
        if w>=3: print(f'  {t} {a}..{b} ({w}px)')

im1 = cv2.imread('ewdx-port/analysis/v24_blackbox/v241_owner_test.png', cv2.IMREAD_COLOR)
cap=cv2.VideoCapture('MuMuSharedFolder/VideoRecords/EchidnaWarsDX(1).mp4')
cap.set(cv2.CAP_PROP_POS_FRAMES,60); ok,f=cap.read(); cap.release()

# disc centers eyeballed from full-frame previews
runs(im1, 250, 380, 860, 'v24.1')
vruns(im1, 590, 40, 500, 'v24.1')
runs(f, 265, 100, 560, 'v25.1')
vruns(f, 320, 40, 500, 'v25.1')

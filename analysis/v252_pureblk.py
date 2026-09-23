import cv2, numpy as np

def profile(img, y, label):
    """Pure-black runs (<12) vs dark (12..55) vs rest on one row."""
    row = img[y].astype(int)
    mean = row.mean(axis=1)
    pure = mean < 12
    runs=[]; i=0
    while i < len(mean):
        j=i
        while j<len(mean) and pure[j]==pure[i]: j+=1
        if j-i>=4: runs.append(('PUREBLK' if pure[i] else 'x', i, j, j-i))
        i=j
    big=[r for r in runs if r[3]>=8]
    print(f'{label} y={y}:', ' | '.join(f'{t} {a}..{b}({w})' for t,a,b,w in big))
    return big

def vprofile(img, x, label):
    col = img[:,x].astype(int).mean(axis=1)
    pure = col < 12
    runs=[]; i=0
    while i < len(col):
        j=i
        while j<len(col) and pure[j]==pure[i]: j+=1
        if j-i>=4: runs.append(('PUREBLK' if pure[i] else 'x', i, j, j-i))
        i=j
    big=[r for r in runs if r[3]>=8]
    print(f'{label} x={x}:', ' | '.join(f'{t} {a}..{b}({w})' for t,a,b,w in big))

im1 = cv2.imread('ewdx-port/analysis/v24_blackbox/v241_owner_test.png', cv2.IMREAD_COLOR)
cap=cv2.VideoCapture('MuMuSharedFolder/VideoRecords/EchidnaWarsDX(1).mp4')
cap.set(cv2.CAP_PROP_POS_FRAMES,60); ok,f=cap.read(); cap.release()

print('=== horizontal through disc center (plate square width) ===')
profile(im1, 250, 'v24.1')
profile(f, 265, 'v25.1')
print('=== vertical through disc center (plate square height) ===')
vprofile(im1, 592, 'v24.1')
vprofile(f, 322, 'v25.1')

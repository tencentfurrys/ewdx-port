import cv2, numpy as np

def corner_stats(img, plate_box, label):
    x0,y0,x1,y1 = plate_box
    crop = img[y0:y1, x0:x1]
    h,w = crop.shape[:2]
    # sample 20x20 patches in the four corners (inside the plate)
    pts = [(6,6),(w-26,6),(6,h-26),(w-26,h-26)]
    vals = [crop[y:y+20,x:x+20].reshape(-1,3).mean(axis=0) for x,y in pts]
    print(label, 'corners BGR:', ' '.join(f'({v[0]:.0f},{v[1]:.0f},{v[2]:.0f})' for v in vals))

def plate_row(img, y, x0, x1, label):
    """Hard-edge plate bounds on one row: plate = longest run of near-black."""
    row = img[y, x0:x1].mean(axis=1)
    blk = row < 55
    best=(0,0,0); i=0
    while i < len(blk):
        if blk[i]:
            j=i
            while j<len(blk) and blk[j]: j+=1
            if j-i > best[2]: best=(i,j,j-i)
            i=j
        else: i+=1
    a,b,_ = best
    print(f'{label} y={y}: plate {x0+a}..{x0+b} (W={b-a})')
    return x0+a, x0+b

im1 = cv2.imread('ewdx-port/analysis/v24_blackbox/v241_owner_test.png', cv2.IMREAD_COLOR)
cap=cv2.VideoCapture('MuMuSharedFolder/VideoRecords/EchidnaWarsDX(1).mp4')
cap.set(cv2.CAP_PROP_POS_FRAMES,60); ok,f=cap.read(); cap.release()
r8 = cv2.imread('ewdx-port/analysis/v21_dense/maw_08.jpg')
r9 = cv2.imread('ewdx-port/analysis/v21_dense/maw_09.jpg')

corner_stats(im1, (395,85,840,430), 'v24.1')
corner_stats(f, (140,90,510,450), 'v25.1')
corner_stats(r8, (0,100,320,510), 'ref08')
corner_stats(r9, (0,100,320,510), 'ref09')

# plate widths through the disc center rows
plate_row(im1, 250, 380, 880, 'v24.1')
plate_row(f, 265, 100, 560, 'v25.1')
plate_row(r8, 300, 0, 510, 'ref08')
plate_row(r9, 300, 0, 510, 'ref09')

import cv2, numpy as np

def ring_plate(img, plate_box, label):
    x0,y0,x1,y1 = plate_box
    crop = img[y0:y1, x0:x1]
    h,w = crop.shape[:2]
    r,g,b = crop[:,:,2].astype(int), crop[:,:,1].astype(int), crop[:,:,0].astype(int)
    silver = (np.maximum(np.maximum(r,g),b)>140) & (np.maximum(np.maximum(r,g),b)-np.minimum(np.minimum(r,g),b)<40)
    red = (r>80) & (r>g+25) & (r>b+25)
    black = (np.maximum(np.maximum(r,g),b)<40)
    # plate = black region extent: largest black run on middle row/col
    midr = h//2; midc = w//2
    def span(m):
        idx=np.where(m)[0]
        return (int(idx[0]), int(idx[-1])) if len(idx) else None
    # find plate horizontally: scan middle row for black->nonblack transitions
    rowb = black[midr]
    # plate bounds: from first black px on the middle row to last
    sb = span(rowb)
    sr = span(red[midr])
    ss = span(silver[midr])
    colb = black[:, midc]
    sbv = span(colb)
    srv = span(red[:, midc])
    ssv = span(silver[:, midc])
    print(f'{label}: plate W={sb[1]-sb[0]+1 if sb else 0} H={sbv[1]-sbv[0]+1 if sbv else 0}')
    if sr: print(f'  red  horiz {sr} width={sr[1]-sr[0]+1}   vert {srv} height={srv[1]-srv[0]+1}')
    if ss: print(f'  ring horiz {ss} width={ss[1]-ss[0]+1}   vert {ssv} height={ssv[1]-ssv[0]+1}')
    if sb and ss:
        pw = sb[1]-sb[0]+1; rw_ = ss[1]-ss[0]+1
        print(f'  RING/PLATE = {rw_/pw:.3f}')

im1 = cv2.imread('ewdx-port/analysis/v24_blackbox/v241_owner_test.png', cv2.IMREAD_COLOR)
cap=cv2.VideoCapture('MuMuSharedFolder/VideoRecords/EchidnaWarsDX(1).mp4')
cap.set(cv2.CAP_PROP_POS_FRAMES,60); ok,f=cap.read(); cap.release()

# plate bounding boxes eyeballed from the crops
ring_plate(im1, (395,85,840,430), 'v24.1')
ring_plate(f, (140,90,510,450), 'v25.1 f60')

# reference: maw_08/maw_09 plates
r8 = cv2.imread('ewdx-port/analysis/v21_dense/maw_08.jpg')
r9 = cv2.imread('ewdx-port/analysis/v21_dense/maw_09.jpg')
# ref crops are 510x510 with plate somewhere; use full frame black extent on middle row
ring_plate(r8, (0,0,510,510), 'ref maw_08')
ring_plate(r9, (0,0,510,510), 'ref maw_09')

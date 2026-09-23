import cv2, numpy as np

def analyze(img, seed, box, label):
    x0,y0,x1,y1 = box
    crop = img[y0:y1, x0:x1]
    ch,cw = crop.shape[:2]
    gray = crop.mean(axis=2).astype(np.uint8)
    dark = gray < 70
    mask = dark.astype(np.uint8)
    n, lab, stats, cent = cv2.connectedComponentsWithStats(mask, 8)
    comp = lab[seed[1]-y0, seed[0]-x0]
    if comp == 0:
        for dy in range(-8,9,2):
            for dx in range(-8,9,2):
                c = lab[seed[1]-y0+dy, seed[0]-x0+dx]
                if c: comp = c; break
            if comp: break
    x,y,ww,hh,area = stats[comp]
    sub = (lab==comp)
    ys,xs = np.where(sub)
    # ring extent inside the component
    r,g,b = crop[:,:,2].astype(int), crop[:,:,1].astype(int), crop[:,:,0].astype(int)
    silver = (np.maximum(np.maximum(r,g),b)>150) & (np.maximum(np.maximum(r,g),b)-np.minimum(np.minimum(r,g),b)<45)
    sil = silver & sub
    ys2,xs2 = np.where(sil)
    rw = (xs2.max()-xs2.min()+1) if len(xs2) else 0
    rh = (ys2.max()-ys2.min()+1) if len(ys2) else 0
    pts = [(6,6),(ww-30,6),(6,hh-30),(ww-30,hh-30)]
    corners = [crop[y+py:y+py+24, x+px:x+px+24].reshape(-1,3).mean(axis=0) for px,py in pts]
    print(f'{label}: plate {ww}x{hh} at crop({x},{y}) ring {rw}x{rh} ring/plate={max(rw,rh)/max(ww,hh):.3f}')
    print('  plate corners: ' + ' '.join(f'({c[0]:.0f},{c[1]:.0f},{c[2]:.0f})' for c in corners))

im1 = cv2.imread('ewdx-port/analysis/v24_blackbox/v241_owner_test.png', cv2.IMREAD_COLOR)
cap=cv2.VideoCapture('MuMuSharedFolder/VideoRecords/EchidnaWarsDX(1).mp4')
cap.set(cv2.CAP_PROP_POS_FRAMES,60); ok,f=cap.read(); cap.release()
analyze(im1, (830, 110), (380, 60, 900, 470), 'v24.1')
analyze(f, (160, 120), (100, 60, 620, 470), 'v25.1')

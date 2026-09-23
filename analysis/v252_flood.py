import cv2, numpy as np

def analyze(img, seed, label):
    h,w = img.shape[:2]
    gray = img.mean(axis=2).astype(np.uint8)
    # flood from seed over near-seed darkness
    seedv = img[seed[1], seed[0]].astype(int)
    diff = np.abs(img.astype(int) - seedv[None,None,:]).sum(axis=2)
    darkish = gray < 90
    mask = (diff < 110) & darkish
    mask = mask.astype(np.uint8)
    n, lab, stats, cent = cv2.connectedComponentsWithStats(mask, 8)
    comp = lab[seed[1], seed[0]]
    if comp == 0:
        # try neighbors
        for dy in range(-6,7,2):
            for dx in range(-6,7,2):
                c = lab[seed[1]+dy, seed[0]+dx]
                if c: comp = c; break
            if comp: break
    x,y,ww,hh,area = stats[comp]
    sub = (lab==comp)
    # ring = bright silver pixels inside plate box
    box = img[y:y+hh, x:x+ww]
    r,g,b = box[:,:,2].astype(int), box[:,:,1].astype(int), box[:,:,0].astype(int)
    silver = (np.maximum(np.maximum(r,g),b)>150) & (np.maximum(np.maximum(r,g),b)-np.minimum(np.minimum(r,g),b)<45)
    ys,xs = np.where(silver)
    rw = (xs.max()-xs.min()+1) if len(xs) else 0
    rh = (ys.max()-ys.min()+1) if len(ys) else 0
    # plate corner patches (inset 8px, 24x24)
    pts = [(8,8),(ww-32,8),(8,hh-32),(ww-32,hh-32)]
    corners = [box[py:py+24,px:px+24].reshape(-1,3).mean(axis=0) for px,py in pts]
    print(f'{label}: plate box ({x},{y}) {ww}x{hh} area={area}')
    print(f'  ring silver extent {rw}x{rh}  ring/plate={max(rw,rh)/max(ww,hh):.3f}')
    print(f'  plate corners: ' + ' '.join(f'({c[0]:.0f},{c[1]:.0f},{c[2]:.0f})' for c in corners))
    return (x,y,ww,hh)

im1 = cv2.imread('ewdx-port/analysis/v24_blackbox/v241_owner_test.png', cv2.IMREAD_COLOR)
cap=cv2.VideoCapture('MuMuSharedFolder/VideoRecords/EchidnaWarsDX(1).mp4')
cap.set(cv2.CAP_PROP_POS_FRAMES,60); ok,f=cap.read(); cap.release()
r8 = cv2.imread('ewdx-port/analysis/v21_dense/maw_08.jpg')
r9 = cv2.imread('ewdx-port/analysis/v21_dense/maw_09.jpg')

analyze(im1, (820, 100), 'v24.1')      # seed: top-right plate area (black per corner probe)
analyze(f, (170, 130), 'v25.1 f60')    # seed: between ring and plate corner
analyze(r8, (150, 200), 'ref08')
analyze(r9, (150, 200), 'ref09')

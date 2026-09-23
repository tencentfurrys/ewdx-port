import cv2, numpy as np, glob

def metrics(img, label):
    H,W = img.shape[:2]
    r,g,b = img[:,:,2].astype(int), img[:,:,1].astype(int), img[:,:,0].astype(int)
    black = (np.maximum(np.maximum(r,g),b) < 45)
    red = (r>90) & (r>b+30) & (r>g+30)
    silver = (np.maximum(np.maximum(r,g),b)>150) & (abs(r-g)<28) & (abs(g-b)<28)
    # find the widest red row = disc center
    best=(0,0)
    for y in range(0,H,2):
        n=red[y].sum()
        if n>best[1]: best=(y,n)
    y=best[0]
    if best[1]<20:
        print(label,'no red disc found'); return
    row_b = black[y]; row_r = red[y]; row_s = silver[y]
    def span(m):
        idx=np.where(m)[0]
        if len(idx)==0: return None
        return idx[0], idx[-1], idx[-1]-idx[0]+1
    sb = span(row_b[idx] if False else row_b)  # full row
    sr = span(row_r)
    ss = span(row_s & (np.arange(W)>=sr[0]-80) & (np.arange(W)<=sr[1]+80)) if sr else None
    # black run that CONTAINS the red span = the plate
    x0,x1 = sr[0], sr[1]
    pl = x0
    while pl>0 and row_b[pl-1]: pl-=1
    pr = x1
    while pr<W-1 and row_b[pr+1]: pr+=1
    print(f'{label}: center y={y}')
    print(f'  red interior {sr[0]}..{sr[1]} ({sr[2]}px)')
    print(f'  plate {pl}..{pr+1} ({pr+1-pl}px)   corner L={sr[0]-pl}px R={pr+1-1-sr[1]}px')
    if ss: print(f'  silver ring span {ss[0]}..{ss[1]} ({ss[2]}px)  red/plate={sr[2]/(pr+1-pl):.3f}')
    return sr[2]/(pr+1-pl)

print('=== WEB REFERENCE (original) ===')
for p in sorted(glob.glob('ewdx-port/analysis/v21_dense/maw_*.jpg')):
    im=cv2.imread(p)
    t=p.split('_')[-1].split('.')[0]
    metrics(im, 'ref '+t)

print('=== PORT v24.1 (owner screenshot) ===')
metrics(cv2.imread('ewdx-port/analysis/v24_blackbox/v241_owner_test.png', cv2.IMREAD_COLOR), 'v24.1')

print('=== PORT v25.1 (video frames) ===')
cap=cv2.VideoCapture('MuMuSharedFolder/VideoRecords/EchidnaWarsDX(1).mp4')
for fn in (30,60,90,120):
    cap.set(cv2.CAP_PROP_POS_FRAMES, fn); ok,f=cap.read()
    if ok: metrics(f, f'v25.1 f{fn}')
cap.release()

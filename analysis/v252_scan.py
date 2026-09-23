import cv2, numpy as np, os
V='MuMuSharedFolder/VideoRecords/EchidnaWarsDX(1).mp4'
OUT='ewdx-port/analysis/v252_evidence'
os.makedirs(OUT, exist_ok=True)
cap=cv2.VideoCapture(V)
frames=[]
while True:
    ok,f=cap.read()
    if not ok: break
    frames.append(f)
cap.release()
n=len(frames); print('frames',n)
# darkness scan (game area 720x540 at x120..840)
rows=[]
for i,f in enumerate(frames):
    g=f[:,120:840]
    dark=(g.mean(axis=2)<40).mean()
    rows.append((i,dark))
for i,d in rows:
    if i%5==0: print(i, round(float(d),3))
# contact sheets: 12 frames per sheet, 4x3, each 480x270
step=max(1,n//24)
picks=list(range(0,n,step))[:24]
for s in range(2):
    chunk=picks[s*12:(s+1)*12]
    if not chunk: break
    sheet=np.full((3*270,4*480,3),255,np.uint8)
    for k,i in enumerate(chunk):
        t=cv2.resize(frames[i],(480,270))
        r,c=divmod(k,4)
        sheet[r*270:(r+1)*270, c*480:(c+1)*480]=t
        cv2.putText(sheet,str(i),(c*480+5,r*270+20),cv2.FONT_HERSHEY_SIMPLEX,0.6,(0,0,255),2)
    cv2.imwrite(f'{OUT}/sheet{s}.png',sheet)
    print('sheet',s,'frames',chunk)

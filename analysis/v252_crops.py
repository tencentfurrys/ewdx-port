import cv2, numpy as np

def lab(im, text):
    im = im.copy()
    cv2.putText(im, text, (8, 24), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0,255,255), 2)
    return im

# v24.1 porthole area (right half of screenshot)
im1 = cv2.imread('ewdx-port/analysis/v24_blackbox/v241_owner_test.png', cv2.IMREAD_COLOR)
c1 = im1[60:460, 380:880]

cap=cv2.VideoCapture('MuMuSharedFolder/VideoRecords/EchidnaWarsDX(1).mp4')
fr={}
for fn in (20,60,100):
    cap.set(cv2.CAP_PROP_POS_FRAMES, fn); ok,f=cap.read()
    if ok: fr[fn]=f
cap.release()
c2 = fr[60][60:460, 100:600]

c1 = cv2.resize(c1, (500,400)); c2 = cv2.resize(c2, (500,400))
sheet1 = np.hstack([lab(c1,'PORT v24.1'), lab(c2,'PORT v25.1 f60')])
cv2.imwrite('ewdx-port/analysis/v252_evidence/cmp_ports.jpg', sheet1, [cv2.IMWRITE_JPEG_QUALITY,85])

# reference: two most informative maw crops upscaled to same height
r1 = cv2.imread('ewdx-port/analysis/v21_dense/maw_08.jpg')
r2 = cv2.imread('ewdx-port/analysis/v21_dense/maw_09.jpg')
r1 = cv2.resize(r1, (400,400)); r2 = cv2.resize(r2, (400,400))
sheet2 = np.hstack([lab(r1,'WEB REF t?'), lab(r2,'WEB REF t?')])
cv2.imwrite('ewdx-port/analysis/v252_evidence/cmp_ref.jpg', sheet2, [cv2.IMWRITE_JPEG_QUALITY,85])
print('done')

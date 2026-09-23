import cv2, numpy as np

s1 = cv2.imread('C:/Users/RDP/AppData/Local/Temp/1/ewdx_v251/assets/data/pic/stom_s1.bmp', cv2.IMREAD_UNCHANGED)
st1 = cv2.imread('C:/Users/RDP/AppData/Local/Temp/1/ewdx_v251/assets/data/pic/stom1.bmp', cv2.IMREAD_UNCHANGED)
print('stom_s1', s1.shape, 'stom1', st1.shape)

def show(img, rects, out, scale=2):
    if img.shape[2] == 4:
        a = img[:,:,3:4].astype(float)/255
        rgb = img[:,:,:3].astype(float)
        comp = (rgb*a + 255*(1-a)).astype(np.uint8)
    else:
        comp = img
    big = cv2.resize(comp, (comp.shape[1]*scale, comp.shape[0]*scale), interpolation=cv2.INTER_NEAREST)
    for (x,y,w,h,name,col) in rects:
        cv2.rectangle(big, (x*scale,y*scale), ((x+w)*scale,(y+h)*scale), col, 2)
        cv2.putText(big, name, (x*scale+2, y*scale+18), cv2.FONT_HERSHEY_SIMPLEX, 0.5, col, 1)
    cv2.imwrite(out, big)
    print(out, big.shape)

show(s1, [
    (160,0,80,80,'p0 ball',(0,255,0)),
    (80,0,80,80,'p35 glow',(255,0,0)),
    (0,0,80,80,'p39 ring',(0,0,255)),
    (68,81,118,94,'p17 wobble-src',(255,0,255)),
], 'ewdx-port/analysis/v252_evidence/atlas_stom_s1.jpg')
show(st1, [
    (0,35,46,82,'p18 L',(0,255,0)),
    (0,35,46,82,'p19 R',(0,0,255)),
], 'ewdx-port/analysis/v252_evidence/atlas_stom1.jpg', scale=3)

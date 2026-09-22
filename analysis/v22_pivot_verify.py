#!/usr/bin/env python3
# Numerical parity check: Ghidra decompile of hmm.dll FUN_10001fa0
# (analysis/decomp_inner.txt) vs the new port emit_quad, all vertices,
# flags in {plain, ctr_anchor} x scale 256..512 x angle 0..192.
#
# Decompile constants: _DAT_1006928c = 0.5, _DAT_10069290 = 1/256.
# Layout: vertices in this[][0xe7c..] as (x,y) pairs, stride 7 floats.
# this+0x13e8=posx(?) 0x13ec=param_1 anchor x, 0x13f0=local_20 anchor y,
# 0x13f4/0x13f8=scale 8.8, 0x13fc=angle index, 0x1410/1414=rect w/h,
# 0x1418/141c=rect x/y, tex dims at id*0x1c+0x80/0x84, vflip at +0x90.
import math
import numpy as np

C05 = 0.5
INV256 = 1.0 / 256.0

def decompile_vertices(dw, dh, dx, dy, scx8, scy8, ang, flags, texW, texH, vflip=0):
    # 0x13f4 = scale x, 0x13f8 = scale y (floats as stored: script ints /1? ->
    # hmm stores the DGSCALEANDANGLE 8.8 raw ints in the float slots; the
    # multiply by _DAT_10069290 (1/256) converts to real scale.)
    local_20 = float(dy)                    # 0x13f0 anchor y (DGPOS y)
    local_1c = float(dw)                    # rect w
    local_18 = float(dh)                    # rect h
    param_1 = float(dx)                     # 0x13ec anchor x (DGPOS x)
    # clip: local_1c = min(w, texW - rx) etc. (rx assumed 0 for the check)
    if (flags & 2) == 0:
        local_c = scx8 * INV256             # scale x real
        fVar9 = scy8 * INV256               # scale y real
    else:
        local_c = scx8 / local_1c
        fVar9 = scy8 / local_18
    if (flags & 1) != 0:
        param_1 = dx - local_1c * C05
        local_20 = dy - local_18 * C05
    # corners (x,y): (0,0),(w,0),(w,h),(0,h) relative to (param_1, local_20)
    cx = [param_1, param_1 + local_1c, param_1 + local_1c, param_1]
    cy = [local_20, local_20, local_20 + local_18, local_20 + local_18]
    ang_i = ang & 0xff
    cth = math.cos(ang_i * math.pi / 128.0)   # LUT: 256 steps = 2pi
    sth = math.sin(ang_i * math.pi / 128.0)
    pivx = param_1 + local_1c * C05
    pivy = local_20 + local_18 * C05
    local_1c_half = local_1c * C05            # local_1c after *0.5 (+param_1 in code)
    out = []
    for k in range(4):
        if (flags & 4) == 0:
            # plain branch: pre-add pivot pair, rotate, scale, anchor
            # (dx-0.5, dy-0.5): verbatim decompile (fVar10=dx-0.5,
            # fVar5=dy-0.5).
            vx = cx[k] - (local_1c_half + param_1) + (local_1c_half)
            vy = cy[k] - (local_18 * C05 + local_20) + (local_18 * C05)
            rx_ = vx * cth - vy * sth
            ry_ = vx * sth + vy * cth
            Xs = local_c * (rx_ + local_1c_half) + (param_1 - C05)
            Ys = fVar9 * (ry_ + local_18 * C05) + (local_20 - C05)
        else:
            # ctr_anchor branch: fVar8 = local_1c*0.5 - 0.5 ... anchor
            vx = cx[k] - (local_1c_half + param_1)
            vy = cy[k] - (local_18 * C05 + local_20)
            # scale first (about pivot-relative coords), then rotate
            sx_ = vx * local_c
            sy_ = vy * fVar9
            rx_ = sx_ * cth - sy_ * sth
            ry_ = sx_ * sth + sy_ * cth
            # fVar10 = local_1c*0.5 + param_1 - 0.5 ; fVar5 = pivy - 0.5
            Xs = rx_ + (local_1c_half + param_1 - C05)
            Ys = ry_ + (local_20 + local_18 * C05 - C05)
        out.append((Xs, Ys))
    return out

def port_vertices(dw, dh, dx, dy, scx8, scy8, ang, flags, texW, texH):
    # exactly the new emit_quad math (flag&2 off path)
    scx = scx8 * INV256
    scy = scy8 * INV256
    pivx = dx + dw * C05
    pivy = dy + dh * C05
    ax_ctr = pivx - C05
    ay_ctr = pivy - C05
    cx = [dx, dx + dw, dx + dw, dx]
    cy = [dy, dy, dy + dh, dy + dh]
    cth = math.cos((ang & 0xff) * math.pi / 128.0)
    sth = math.sin((ang & 0xff) * math.pi / 128.0)
    out = []
    for k in range(4):
        X = cx[k] - pivx
        Y = cy[k] - pivy
        Xr = X * cth - Y * sth
        Yr = X * sth + Y * cth
        if flags & 4:
            Xsc = X * scx; Ysc = Y * scy
            Xs = Xsc * cth - Ysc * sth + ax_ctr
            Ys = Xsc * sth + Ysc * cth + ay_ctr
        else:
            Xs = (Xr + dw * C05) * scx + dx - C05
            Ys = Yr * scy + pivy - C05
        out.append((Xs, Ys))
    return out

def key(pts):
    return sorted((round(x, 4), round(y, 4)) for x, y in pts)

bad = 0
cases = 0
for flags in (0, 4):
    for sc in (256, 320, 512, 128):
        for ang in (0, 32, 64, 128, 192, 255):
            for (dw, dh, dx, dy) in ((70, 70, 100, 80), (24, 90, 300, 40), (80, 80, 40, 60)):
                a = key(decompile_vertices(dw, dh, dx, dy, sc, sc, ang, flags, 512, 512))
                b = key(port_vertices(dw, dh, dx, dy, sc, sc, ang, flags, 512, 512))
                cases += 1
                if a != b:
                    bad += 1
                    if bad <= 3:
                        print("MISMATCH flags=%d sc=%d ang=%d rect=%dx%d@(%d,%d)" % (
                            flags, sc, ang, dw, dh, dx, dy))
                        for pa, pb in zip(a, b):
                            print("   orig %-22s port %-22s" % (str(pa), str(pb)))
print("cases: %d, mismatches: %d" % (cases, bad))

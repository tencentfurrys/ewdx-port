#!/usr/bin/env python3
# Numerical parity check: Ghidra decompile of hmm.dll FUN_10001fa0
# (analysis/decomp_inner.txt) vs the port emit_quad (ewdx_ndk/ewdx_batch.cpp),
# all vertices, flags in {plain, ctr_anchor} x scale 128..512 x angle 0..255.
#
# Session D (2026-09-22) rewrite — session C left the PLAIN (flags==0) branch
# of BOTH test sides stale (a v19-era translation: the decompile side rotated
# about dx instead of the pivot; the port side left dh/2 outside the scale).
# Both sides below are now line-for-line with the sources:
#
#   decompile (decomp_inner.txt, plain = (bVar13&4)==0 loop):
#       x' = x - (w*0.5 + ax); y' = y - (h*0.5 + ay)      // pivot-relative
#       rotate; add pivot back; SUBTRACT (ax, ay)         // -> (rx+w/2, ry+h/2)
#       scale; add (ax-0.5, ay-0.5)
#   decompile (ctr_anchor = (bVar13&4)!=0 loop):
#       x' = x - (w*0.5 + ax); y' = y - (h*0.5 + ay)
#       SCALE pivot-relative coords; rotate; add (w*0.5+ax-0.5, h*0.5+ay-0.5)
#     (= anchor (pivx-0.5, pivy-0.5); the v22 fix — v20/v21 anchored x at
#      dx-0.5, the constant -dw/2 "head off / door floating" shift)
#   port (ewdx_batch.cpp emit_quad, verbatim same two branches; ax/ay are the
#     flag&1-adjusted dx/dy done by ewdx_copy_flags before the call).
#
# Plain grows down-right from the TOP-LEFT corner (verbatim, do not "fix");
# ctr_anchor is identity-equivalent at rest and center-growing when
# scaled/rotated. At rest BOTH place the rect at (cx-0.5, cy-0.5).
#
# Decompile constants: _DAT_1006928c = 0.5, _DAT_10069290 = 1/256.
# Layout: vertices in this[][0xe7c..] as (x,y) pairs, stride 7 floats.
# this+0x13e8=posx(?) 0x13ec=param_1 anchor x, 0x13f0=local_20 anchor y,
# 0x13f4/0x13f8=scale 8.8, 0x13fc=angle index, 0x1410/1414=rect w/h,
# 0x1418/141c=rect x/y, tex dims at id*0x1c+0x80/0x84, vflip at +0x90.
import math

C05 = 0.5
INV256 = 1.0 / 256.0

def decompile_vertices(dw, dh, dx, dy, scx8, scy8, ang, flags, texW, texH, vflip=0):
    # 0x13ec anchor x, 0x13f0 anchor y (DGPOS), 0x1410/14 rect w/h,
    # 0x13f4/f8 scale 8.8 raw ints (the *1/256 converts to real scale),
    # 0x13fc angle index. rx/ry assumed 0 (texW/texH >= rect, no clip).
    ax = float(dx)                          # param_1   (0x13ec)
    ay = float(dy)                          # local_20  (0x13f0)
    w = float(dw)                           # local_1c  (0x1418)
    h = float(dh)                           # local_18  (0x141c)
    # clip: w = min(w, texW - rx) etc. (no-op for these cases)
    if texW < w:
        w = float(texW)
    if texH < h:
        h = float(texH)
    if (flags & 2) == 0:
        scx = scx8 * INV256                 # local_c
        scy = scy8 * INV256                 # fVar9
    else:
        scx = scx8 / w
        scy = scy8 / h
    if (flags & 1) != 0:
        ax = ax - w * C05
        ay = ay - h * C05
    # corners (x,y) relative to (ax, ay), written to this[][0xe7c..] BEFORE
    # the branch (0xe7c/0xe80, 0xe98/0xe9c, 0xeb4/0xeb8, 0xed0/0xed4):
    cx = [ax, ax + w, ax + w, ax]
    cy = [ay, ay, ay + h, ay + h]
    # local_1c = w*0.5 + ax AFTER the corner writes (pivot x, both branches)
    pvx = w * C05 + ax
    ang_i = ang & 0xff
    cth = math.cos(ang_i * math.pi / 128.0)   # LUT: 256 steps = 2pi
    sth = math.sin(ang_i * math.pi / 128.0)
    out = []
    for k in range(4):
        if (flags & 4) == 0:
            # ---- plain branch, verbatim ((bVar13 & 4) == 0 loop) ----
            # fVar8 = h*0.5 + ay (pivot y); fVar10 = ax-0.5; fVar5 = ay-0.5
            pvy = h * C05 + ay
            vx = cx[k] - pvx                 # subtract pivot (local_1c/fVar8)
            vy = cy[k] - pvy
            rx_ = vx * cth - vy * sth        # LUT rotate
            ry_ = vx * sth + vy * cth
            rx_ = pvx + rx_                  # add pivot back
            ry_ = pvy + ry_
            rx_ = rx_ - ax                   # subtract anchor -> rx + w/2
            ry_ = ry_ - ay                   #                ry + h/2
            Xs = scx * rx_ + (ax - C05)      # scale, anchor (ax-0.5, ay-0.5)
            Ys = scy * ry_ + (ay - C05)
        else:
            # ---- ctr_anchor branch, verbatim ((bVar13 & 4) != 0 loop) ----
            # fVar8 = pvx - 0.5 (w*0.5 + ax - 0.5); local_20 = pvy;
            # fVar10 = pvy - 0.5 (h*0.5 + ay - 0.5)
            pvy = h * C05 + ay
            vx = cx[k] - pvx                 # pivot-relative
            vy = cy[k] - pvy
            sx_ = scx * vx                   # SCALE first (about rect center)
            sy_ = scy * vy
            rx_ = sx_ * cth - sy_ * sth      # then rotate
            ry_ = sx_ * sth + sy_ * cth
            Xs = rx_ + (pvx - C05)           # anchor (pivx-0.5, pivy-0.5)
            Ys = ry_ + (pvy - C05)
        out.append((Xs, Ys))
    return out

def port_vertices(dw, dh, dx, dy, scx8, scy8, ang, flags, texW, texH):
    # Exactly ewdx_ndk/ewdx_batch.cpp emit_quad (v22): dx/dy arrive already
    # flag&1-adjusted from ewdx_copy_flags, scx/scy already flag&2-resolved,
    # and the rx/rw upper clip is a no-op for these cases.
    scx = scx8 * INV256
    scy = scy8 * INV256
    ax, ay = float(dx), float(dy)
    w, h = float(dw), float(dh)
    pivx = ax + w * C05
    pivy = ay + h * C05
    cx = [ax, ax + w, ax + w, ax]
    cy = [ay, ay, ay + h, ay + h]
    cth = math.cos((ang & 0xff) * math.pi / 128.0)
    sth = math.sin((ang & 0xff) * math.pi / 128.0)
    out = []
    for k in range(4):
        X = cx[k] - pivx
        Y = cy[k] - pivy
        Xr = X * cth - Y * sth
        Yr = X * sth + Y * cth
        if flags & 4:
            # ctr_anchor branch (v22, decompile-exact)
            Xsc = X * scx
            Ysc = Y * scy
            Xs = Xsc * cth - Ysc * sth + (pivx - C05)
            Ys = Xsc * sth + Ysc * cth + (pivy - C05)
        else:
            # plain branch (verbatim, pre-v22-exact port form)
            Xd = Xr + pivx - ax
            Yd = Yr + pivy - ay
            Xs = Xd * scx + ax - C05
            Ys = Yd * scy + ay - C05
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

# Sanity pins (catch a test-side regression that keeps both sides equal):
# - ctr_anchor at rest == (cx-0.5, cy-0.5); plain at rest == same placement.
rest = decompile_vertices(80, 80, 40, 60, 256, 256, 0, 4, 512, 512)
want = sorted([(39.5, 59.5), (119.5, 59.5), (119.5, 139.5), (39.5, 139.5)])
assert key(rest) == want, "ctr_anchor rest placement moved: %s" % (key(rest),)
rest0 = decompile_vertices(80, 80, 40, 60, 256, 256, 0, 0, 512, 512)
assert key(rest0) == want, "plain rest placement moved: %s" % (key(rest0),)
# - v20/v21 regression probe: anchor x at dx-0.5 must NOT match the port
#   (guards against re-shipping the head-off bug while looking green).
def v21_vertices(dw, dh, dx, dy, scx8, scy8, ang, flags, texW, texH):
    pts = port_vertices(dw, dh, dx, dy, scx8, scy8, ang, flags, texW, texH)
    if flags & 4:
        pts = [(x - dw * 0.5, y) for (x, y) in pts]  # the v20/v21 -dw/2 shift
    return pts
probe = key(v21_vertices(80, 80, 40, 60, 512, 512, 0, 4, 512, 512))
assert probe != key(port_vertices(80, 80, 40, 60, 512, 512, 0, 4, 512, 512)), \
    "v21 regression probe is insensitive!"
print("sanity pins OK (rest placement + v21 regression probe)")

#!/usr/bin/env python3
"""Do the particle motion vectors point where the particles really move?  (docs/PARTICLE_MV.md, session 15)

Input: a recording of scripts/x3-scene.sh made with AB=1 (FSR4_PARTICLE_MV=1 FSR4_PARTICLE_AB=1): per frame k, planes WITH particle vectors (d_/m_) and WITHOUT (da_/ma_) of the SAME frame, plus the
rendered colour frames (frames/sc_k.png, HUD hidden). Planes are bottom-up like the game's targets; PNGs are flipped to match.
For every consecutive pair (k-1 -> k): particle pixels = pixels where the plane with particle vectors differs from the one without (exactly what the prep pass wrote). The true motion of those
pixels is measured on the colour frames with dense optical flow (Farneback; cur -> prev, so that flow == mv = prev - cur). Compared per pixel: |mv_A - flow| (camera-only vector) and |mv_B - flow|
(particle vector), and the least-squares slope / correlation of mv_B on flow (scale and sign, per axis). Also reported: the share of particle pixels whose own flow is reliable
(forward-backward consistent) and the fraction of the mask where B is closer than A.
usage: mv_vs_flow.py SCENE_DIR [step]
"""
import sys, os, numpy as np, cv2
D = sys.argv[1]; step = int(sys.argv[2]) if len(sys.argv) > 2 else 1; W, H = 1280, 720
n = len([f for f in os.listdir(f"{D}/frames") if f.endswith(".png")])
img = lambda k: cv2.cvtColor(cv2.imread(f"{D}/frames/sc_{k:03d}.png"), cv2.COLOR_BGR2GRAY)[::-1].copy()
mvp = lambda p, k: np.fromfile(f"{D}/planes/{p}_{k:04d}.bin", dtype=np.float16).astype(np.float32).reshape(H, W, 2)
dpl = lambda p, k: np.fromfile(f"{D}/planes/{p}_{k:04d}.bin", dtype=np.float32).reshape(H, W)
ts = lambda k: int(open(f"{D}/planes/cam_{k:04d}.txt").read().split()[-1])
rows = []
for k in range(3, n - 1, step):
    try:
        mB, mA = mvp("m", k), mvp("ma", k); dB, dA = dpl("d", k), dpl("da", k)
    except FileNotFoundError:
        continue
    mask = (np.abs(mB - mA).sum(-1) > 1e-4) | (dB != dA)
    if mask.sum() < 300: continue
    g1, g0 = img(k), img(k - 1)
    fl = cv2.calcOpticalFlowFarneback(g1, g0, None, 0.5, 4, 21, 5, 7, 1.5, 0)          # where each pixel of frame k was in frame k-1
    fb = cv2.calcOpticalFlowFarneback(g0, g1, None, 0.5, 4, 21, 5, 7, 1.5, 0)
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    back = cv2.remap(fb, xx + fl[..., 0], yy + fl[..., 1], cv2.INTER_LINEAR)
    cons = np.hypot(fl[..., 0] + back[..., 0], fl[..., 1] + back[..., 1]) < 1.0
    tex = cv2.Laplacian(cv2.GaussianBlur(g1, (0, 0), 1.0), cv2.CV_32F); tex = cv2.GaussianBlur(np.abs(tex), (0, 0), 3) > 1.0
    m = mask & cons & tex
    if m.sum() < 200: continue
    eA = np.hypot(*(mA[m] - fl[m]).T); eB = np.hypot(*(mB[m] - fl[m]).T); dt = (ts(k) - ts(k - 1))
    fx, fy, bx, by = fl[m][:, 0], fl[m][:, 1], mB[m][:, 0], mB[m][:, 1]
    dBx, dBy = bx - mA[m][:, 0], by - mA[m][:, 1]; rx, ry = fx - mA[m][:, 0], fy - mA[m][:, 1]   # vectors RELATIVE to the camera motion: own particle motion measured vs written
    def slope(a, b): return float((a * b).sum() / max(1e-9, (a * a).sum()))
    rows.append((k, int(mask.sum()), int(m.sum()), float(eA.mean()), float(eB.mean()), float((eB < eA).mean()), float(np.abs(dBx).mean()), float(np.abs(dBy).mean()),
                 float(np.abs(rx).mean()), float(np.abs(ry).mean()), slope(rx, dBx), slope(ry, dBy), dt))
print("k   mask  reliable | err_cam-only err_particle  B<A | written |dx| |dy| | measured-rel |dx| |dy| | slope x y (written on measured, 1 = exact) | dt")
for r in rows: print(f"{r[0]:3d} {r[1]:6d} {r[2]:6d} | {r[3]:6.2f} {r[4]:6.2f} {r[5]:5.2f} | {r[6]:6.2f} {r[7]:6.2f} | {r[8]:6.2f} {r[9]:6.2f} | {r[10]:6.2f} {r[11]:6.2f} | {r[12]}")
if rows:
    a = np.array(rows); w = a[:, 2]
    wm = lambda c: float((a[:, c] * w).sum() / w.sum())
    print(f"\nWEIGHTED over {len(rows)} frames: err cam-only {wm(3):.2f} px, err particle {wm(4):.2f} px, share B<A {wm(5):.2f}; written |d| {wm(6):.2f}/{wm(7):.2f} vs measured-relative {wm(8):.2f}/{wm(9):.2f}; slope {wm(10):.2f}/{wm(11):.2f}")

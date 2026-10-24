#!/usr/bin/env python3
"""fgx3_proto.py NATIVE_DIR [FOV_DEG]: OFFLINE prototype of a x3 intermediate-frame generator (NOT neural, NOT integrated in the game).
For the camera-pan phase of scripts/motion-test.sh (native frames 95..179, yaw step 0.3 deg/frame) take real frames F0=k and F3=k+3 and synthesise t=1/3 and t=2/3 by
pure-rotation reprojection (homography K R K^-1, exact for a rotating camera at any depth), blending the two warped sources by time. Ground truth = the natively rendered frames k+1, k+2
(same deterministic state). Baselines: repeat the nearest real frame (frame duplication - what must NOT be advertised as generation) and a plain 50/50/time-weighted blend (no motion).
Metrics on a region free of HUD/chat/toast: PSNR and SSIM (7x7 box). The focal length is calibrated once on (k,k+1) pairs, not on the evaluated frames."""
import sys, numpy as np
from PIL import Image
def L(d, k): return Image.open(f"{d}/mo_{k:03d}.png").convert('RGB')
def box(x, r=3):
    k = 2 * r + 1; c = np.pad(x, ((r + 1, r), (r + 1, r)), mode='edge').cumsum(0).cumsum(1); return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / (k * k)
def ssim(a, b):
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2; ma, mb = box(a), box(b); va = box(a * a) - ma * ma; vb = box(b * b) - mb * mb; cab = box(a * b) - ma * mb
    return float((((2 * ma * mb + c1) * (2 * cab + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2))).mean())
def lum(im): return np.asarray(im.convert('L'), dtype=np.float64)
def psnr(a, b): m = ((a - b) ** 2).mean(); return 99.0 if m == 0 else 10 * np.log10(255 ** 2 / m)
def warp(im, f, yaw_deg):
    """Image of the same scene as seen by a camera yawed by yaw_deg relative to the source camera (target pixel -> source pixel via H = K R K^-1)."""
    W, H = im.size; cx, cy = W / 2, H / 2; K = np.array([[f, 0, cx], [0, f, cy], [0, 0, 1]]); t = np.radians(yaw_deg)
    R = np.array([[np.cos(t), 0, np.sin(t)], [0, 1, 0], [-np.sin(t), 0, np.cos(t)]]); M = K @ R @ np.linalg.inv(K); M = M / M[2, 2]
    return im.transform(im.size, Image.PERSPECTIVE, tuple(M.flatten()[:8]), Image.BILINEAR)
nat = sys.argv[1]; STEP = 0.3; ROI = (slice(100, 480), slice(520, 1260))
W = 1280; vfov = np.radians(float(sys.argv[2]) if len(sys.argv) > 2 else 70.0); f0 = (720 / 2) / np.tan(vfov / 2)
# calibrate focal length and rotation sign on pairs (k, k+1) of the pan phase (disjoint from the evaluated k values below)
best = None
for sign in (1, -1):
    for f in np.linspace(f0 * 0.8, f0 * 1.25, 46):
        e = np.mean([((lum(warp(L(nat, k), f, sign * STEP)) - lum(L(nat, k + 1)))[ROI] ** 2).mean() for k in range(96, 110, 4)])
        if best is None or e < best[0]: best = (e, sign, f)
_, sign, f = best; print(f"calibration: rotation sign {sign:+d}, focal {f:.1f} px (from vfov {np.degrees(vfov):.0f} deg: {f0:.1f}); warp MSE on (k,k+1) = {best[0]:.2f}")
rows = {n: [] for n in ("duplicate", "blend", "warp")}; srows = {n: [] for n in ("duplicate", "blend", "warp")}
for k in range(115, 170, 3):
    F0, F3 = L(nat, k), L(nat, k + 3)
    for j, t in ((1, 1 / 3), (2, 2 / 3)):
        truth = lum(L(nat, k + j))
        w0 = warp(F0, f, sign * STEP * j); w3 = warp(F3, f, -sign * STEP * (3 - j))      # both sources reprojected to the target time
        cand = {"duplicate": lum(F0 if t < 0.5 else F3), "blend": (1 - t) * lum(F0) + t * lum(F3), "warp": (1 - t) * lum(w0) + t * lum(w3)}
        for n, img in cand.items(): rows[n].append(psnr(img[ROI], truth[ROI])); srows[n].append(ssim(img[ROI], truth[ROI]))
print("generated-frame quality vs natively rendered truth (mean over %d synthesised frames):" % len(rows['warp']))
for n in ("duplicate", "blend", "warp"): print(f"  {n:9s} PSNR {np.mean(rows[n]):6.2f} dB (min {np.min(rows[n]):.2f})  SSIM {np.mean(srows[n]):.4f}")

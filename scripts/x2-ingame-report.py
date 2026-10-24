#!/usr/bin/env python3
"""x2-ingame-report.py COARSE_DIR DENSE2_DIR: in-game FG x2 (AMD FG 4.0.1) quality against TRUE intermediate frames. (derived from x3-ingame-report.py)
COARSE = scripts/motion-test.sh FG=true MOTION_GEN=1 (generated frame mog_j is shown BEFORE real frame j), DENSE2 = same motion at HALF step without FG. Original x3 text:
in-game FG x3 quality. COARSE = scripts/motion-test.sh FGMULT=3 FSR4_X3_CAPTURE=1 (real frames mo_j, generated x3g_j_1 / x3g_j_2 shown BEFORE real frame j),
DENSE = same deterministic motion at 1/3 step without FG (mo_i, i=3j: the real frames; 3j-2 and 3j-1: the TRUTH of the two generated frames).
PSNR/SSIM in a HUD-free ROI (camera-pan phase) and in the sheep ROI (entity phase); baselines: duplicate nearest real frame and plain time blend."""
import sys, numpy as np
from PIL import Image
C, Dn = sys.argv[1], sys.argv[2]
ALIGN = (slice(100, 420), slice(520, 1260))   # the dense run's state index of every real frame is found by image matching (the harness phase differs by a frame or two between runs)
_dense = {}
def dense_img(i):
    if i not in _dense: _dense[i] = np.asarray(Image.open(f"{Dn}/mo_{i:03d}.png").convert('L'), dtype=np.float64)
    return _dense[i]
def match(j):
    c = np.asarray(Image.open(f"{C}/mo_{j:03d}.png").convert('L'), dtype=np.float64)[ALIGN]
    return min((((dense_img(i)[ALIGN] - c) ** 2).mean(), i) for i in range(max(0, 2 * j - 8), min(119, 2 * j + 4)))[1]
def L(d, n, k): return np.asarray(Image.open(f"{d}/{n}_{k:03d}.png").convert('L'), dtype=np.float64)
def Lg(d, j, i=None): return np.asarray(Image.open(f"{d}/mog_{j:03d}.png").convert('L'), dtype=np.float64)
def box(x, r=3):
    k = 2 * r + 1; c = np.pad(x, ((r + 1, r), (r + 1, r)), mode='edge').cumsum(0).cumsum(1); return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / (k * k)
def ssim(a, b):
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2; ma, mb = box(a), box(b); va = box(a * a) - ma * ma; vb = box(b * b) - mb * mb; cab = box(a * b) - ma * mb
    return float((((2 * ma * mb + c1) * (2 * cab + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2))).mean())
def psnr(a, b): m = ((a - b) ** 2).mean(); return 99.0 if m == 0 else 10 * np.log10(255 ** 2 / m)
OFF = int(np.median([match(j) - 2 * j for j in range(33, 58, 3)])); print(f"dense-run alignment offset (from the camera-pan frames, image matching): {OFF} dense frames")
for name, js, roi in (("entity phase (sheep ROI)", range(5, 29), (slice(430, 545), slice(560, 1000))), ("camera pan (HUD-free ROI)", range(33, 58), (slice(100, 480), slice(520, 1260)))):
    r = {k: [] for k in ("generated", "duplicate", "blend")}; s = {k: [] for k in r}; n = 0
    for j in js:
        try: R0, R1 = L(C, 'mo', j - 1), L(C, 'mo', j)
        except FileNotFoundError: continue
        for i in (1,):
            try: G = Lg(C, j, i); T = L(Dn, 'mo', 2 * j + OFF - 1)
            except FileNotFoundError: continue
            t = 0.5; n += 1
            for k, im in (("generated", G), ("duplicate", R0 if t < 0.5 else R1), ("blend", (1 - t) * R0 + t * R1)): r[k].append(psnr(im[roi], T[roi])); s[k].append(ssim(im[roi], T[roi]))
    print(f"{name}: {n} generated frames")
    for k in r: print(f"  {k:9s} PSNR {np.mean(r[k]):6.2f} dB (min {np.min(r[k]):.2f})  SSIM {np.mean(s[k]):.4f}")

#!/usr/bin/env python3
"""x3-ingame-report.py COARSE_DIR DENSE_DIR [N=3]:  (N = FG multiplier of the coarse run: dense run is at 1/N step; the N-1 generated frames per real frame are compared with the dense frames N*j-(N-1)..N*j-1)
 in-game FG x3 quality. COARSE = scripts/motion-test.sh FGMULT=3 FSR4_X3_CAPTURE=1 (real frames mo_j, generated x3g_j_1 / x3g_j_2 shown BEFORE real frame j),
DENSE = same deterministic motion at 1/3 step without FG (mo_i, i=3j: the real frames; 3j-2 and 3j-1: the TRUTH of the two generated frames).
PSNR/SSIM in a HUD-free ROI (camera-pan phase) and in the sheep ROI (entity phase); baselines: duplicate nearest real frame and plain time blend."""
import sys, numpy as np
from PIL import Image
C, Dn = sys.argv[1], sys.argv[2]; N = int(sys.argv[3]) if len(sys.argv) > 3 else 3
ALIGN = (slice(100, 420), slice(520, 1260))   # the dense run's state index of every real frame is found by image matching (the harness phase differs by a frame or two between runs)
_dense = {}
def dense_img(i):
    if i not in _dense: _dense[i] = np.asarray(Image.open(f"{Dn}/mo_{i:03d}.png").convert('L'), dtype=np.float64)
    return _dense[i]
def match(j):
    c = np.asarray(Image.open(f"{C}/mo_{j:03d}.png").convert('L'), dtype=np.float64)[ALIGN]
    return min((((dense_img(i)[ALIGN] - c) ** 2).mean(), i) for i in range(max(0, N * j - 3 * N), min(60 * N - 1, N * j + 4)))[1]
def L(d, n, k): return np.asarray(Image.open(f"{d}/{n}_{k:03d}.png").convert('L'), dtype=np.float64)
def Lg(d, j, i): return np.asarray(Image.open(f"{d}/x3g_{j:03d}_{i}.png").convert('L'), dtype=np.float64)
def box(x, r=3):
    k = 2 * r + 1; c = np.pad(x, ((r + 1, r), (r + 1, r)), mode='edge').cumsum(0).cumsum(1); return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / (k * k)
def ssim(a, b):
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2; ma, mb = box(a), box(b); va = box(a * a) - ma * ma; vb = box(b * b) - mb * mb; cab = box(a * b) - ma * mb
    return float((((2 * ma * mb + c1) * (2 * cab + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2))).mean())
def psnr(a, b): m = ((a - b) ** 2).mean(); return 99.0 if m == 0 else 10 * np.log10(255 ** 2 / m)
OFF = int(np.median([match(j) - N * j for j in range(33, 58, 3)])); print(f"dense-run alignment offset (from the camera-pan frames, image matching): {OFF} dense frames")
for name, js, roi in (("entity phase (sheep ROI)", range(5, 29), (slice(430, 545), slice(560, 1000))), ("camera pan (HUD-free ROI)", range(33, 58), (slice(100, 480), slice(520, 1260)))):
    r = {k: [] for k in ("generated", "duplicate", "blend")}; s = {k: [] for k in r}; fl = {k: [] for k in r}; gross = {k: [] for k in r}; n = 0
    for j in js:
        try: R0, R1 = L(C, 'mo', j - 1), L(C, 'mo', j)
        except FileNotFoundError: continue
        try: Gs = [Lg(C, j, q) for q in range(1, N)]; Ts = [L(Dn, 'mo', N * j + OFF - (N - q)) for q in range(1, N)]
        except FileNotFoundError: continue
        for i in range(1, N):
            G = Gs[i - 1]; T = Ts[i - 1]; t = i / N; n += 1
            for k, im in (("generated", G), ("duplicate", R0 if t < 0.5 else R1), ("blend", (1 - t) * R0 + t * R1)): r[k].append(psnr(im[roi], T[roi])); s[k].append(ssim(im[roi], T[roi])); gross[k].append(float((np.abs(im[roi] - T[roi]) > 40).mean()) * 100)
        tseq = [R0] + Ts + [R1]
        for k, imgs in (("generated", Gs), ("duplicate", [R0 if q / N < 0.5 else R1 for q in range(1, N)]), ("blend", [(1 - q / N) * R0 + (q / N) * R1 for q in range(1, N)])):
            seq = [R0] + imgs + [R1]; fl[k].append(np.mean([np.abs(seq[q + 1][roi] - seq[q][roi]).mean() for q in range(N)]) / max(1e-9, np.mean([np.abs(tseq[q + 1][roi] - tseq[q][roi]).mean() for q in range(N)])))
    print(f"{name}: {n} generated frames")
    for k in r: print(f"  {k:9s} PSNR {np.mean(r[k]):6.2f} dB (min {np.min(r[k]):.2f})  SSIM {np.mean(s[k]):.4f} | gross-error px {np.mean(gross[k]):.3f} % | flicker x{np.mean(fl[k]):.3f}")

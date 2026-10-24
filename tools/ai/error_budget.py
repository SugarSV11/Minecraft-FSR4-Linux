#!/usr/bin/env python3
"""error_budget.py: where does the error of the generated frames live? (explains why a small local network cannot add PSNR)
For every held-out case (phase B, camera moving) of the recorded scene suite: share of the squared error that comes from the few pixels with |error| > T, and the PSNR that would
result in two idealised cases: (a) a perfect DENOISER that removes every error below T (what a small local residual network can at best do), (b) a perfect fixer of the gross errors
(|error| >= T, i.e. motion/occlusion failures) that leaves the small errors untouched."""
import sys as _sys, os as _os; _sys.path.insert(0, _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), ".")); import ncpaths as NC
import os, sys, numpy as np
E = NC.evidence(9); W, H = 1280, 720
def rd(p): a = np.fromfile(p, dtype=np.uint8).reshape(H, W, 4)[..., :3].astype(np.float64); return 0.299 * a[..., 0] + 0.587 * a[..., 1] + 0.114 * a[..., 2]
def psnr(m): return 99.0 if m == 0 else 10 * np.log10(255 ** 2 / m)
T = 16
for sc in ("disocclusion", "transparency", "particles", "animals", "water", "vegetation", "baseline"):
    cases = [l.strip() for l in open(f"{E}/{sc}/cases/cases.txt") if l.strip()]; cases = [c for c in cases if int(os.path.basename(c)[1:]) >= 60][::2]
    share, base, a, b, gross = [], [], [], [], []
    for c in cases:
        for i in (1, 2):
            g, y = rd(f"{c}/out_{i}.rgba"), rd(f"{c}/truth{i}.rgba"); e = (g - y) ** 2; m = e.mean(); big = np.abs(g - y) >= T
            share.append(e[big].sum() / max(e.sum(), 1e-9)); gross.append(big.mean() * 100); base.append(psnr(m))
            a.append(psnr(e[big].sum() / e.size)); b.append(psnr(e[~big].sum() / e.size))
    print(f"{sc:13s} n={len(base):3d} PSNR {np.mean(base):5.2f} dB | gross-error px (>={T} levels) {np.mean(gross):5.2f} % of pixels carry {100*np.mean(share):5.1f} % of the squared error | "
          f"perfect denoiser (<{T} removed): {np.mean(a):5.2f} dB | perfect gross-error fixer: {np.mean(b):5.2f} dB")

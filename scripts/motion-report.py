#!/usr/bin/env python3
"""motion-report.py NATIVE_DIR MODE_DIR...: compares per-frame images of scripts/motion-test.sh (same frame index = same game state).
Phase A (frames 5-88, static camera, sheep crossing): ghost zone = pixels dark now in the native frame (<35) that were bright (>100) in any of the previous 8 native frames.
  trail = mean(mode - native) luminance over that zone; ghost-pixel fraction = share of zone pixels where mode exceeds native by >25 levels.
Phase B (frames 95-179, camera pan over leaves/water/glass): PSNR / SSIM (7x7 box, luminance) vs native at the same index, and temporal flicker ratio
  = mean|f_t - f_{t-1}| of the mode / same for native (1.0 = same temporal activity as native)."""
import sys, numpy as np
from PIL import Image
def L(d, k): return np.asarray(Image.open(f"{d}/mo_{k:03d}.png").convert('L'), dtype=np.float64)
def box(x, r=3):
    k = 2 * r + 1; c = np.pad(x, ((r + 1, r), (r + 1, r)), mode='edge').cumsum(0).cumsum(1)
    return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / (k * k)
def ssim(a, b):
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2; ma, mb = box(a), box(b); va = box(a * a) - ma * ma; vb = box(b * b) - mb * mb; cab = box(a * b) - ma * mb
    return float((((2 * ma * mb + c1) * (2 * cab + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2))).mean())
def psnr(a, b): m = ((a - b) ** 2).mean(); return 99.0 if m == 0 else 10 * np.log10(255 ** 2 / m)
nat = sys.argv[1]; N = [L(nat, k) for k in range(180)]
for d in sys.argv[2:]:
    F = [L(d, k) for k in range(180)]; name = d.rstrip('/').split('/')[-1]; print(f"== {name}")
    trail, frac, zs = [], [], []
    for k in range(9, 89):
        z = (N[k] < 35) & (np.max(N[k - 8:k], axis=0) > 100); n = int(z.sum())
        if n < 50: continue
        trail.append(float((F[k][z] - N[k][z]).mean())); frac.append(float((F[k][z] > N[k][z] + 25).mean())); zs.append(n)
    if trail: print(f" phase A ghost zone: {len(trail)} frames, avg zone {np.mean(zs):.0f} px | trail lum mean {np.mean(trail):.2f} max {np.max(trail):.2f} | ghost-pixel fraction mean {np.mean(frac)*100:.2f}% max {np.max(frac)*100:.2f}%")
    else: print(" phase A: no ghost-zone frames")
    ez, ee = [], []   # entity zone: native pixels brighter than 140 (the white sheep) dilated by 10 px; PSNR / mean abs error of the mode inside it
    for k in range(9, 89):
        m = box((N[k] > 140).astype(np.float64), r=10) > 0
        if m.sum() < 200: continue
        e = (F[k][m] - N[k][m]) ** 2; ez.append(10 * np.log10(255 ** 2 / max(e.mean(), 1e-9))); ee.append(float(np.abs(F[k][m] - N[k][m]).mean()))
    if ez: print(f" phase A entity zone: {len(ez)} frames | PSNR {np.mean(ez):.2f} dB (min {np.min(ez):.2f}) | mean abs err {np.mean(ee):.2f} levels")
    pa = [psnr(F[k], N[k]) for k in range(5, 89)]; sa = [ssim(F[k], N[k]) for k in range(5, 89, 4)]
    print(f" phase A whole-frame: PSNR {np.mean(pa):.2f} dB (min {np.min(pa):.2f}) SSIM {np.mean(sa):.4f}")
    pb = [psnr(F[k], N[k]) for k in range(95, 180)]; sb = [ssim(F[k], N[k]) for k in range(95, 180, 4)]
    fl = np.mean([np.abs(F[k] - F[k - 1]).mean() for k in range(95, 180)]) / np.mean([np.abs(N[k] - N[k - 1]).mean() for k in range(95, 180)])
    print(f" phase B (pan): PSNR {np.mean(pb):.2f} dB (min {np.min(pb):.2f}, std {np.std(pb):.2f}) SSIM {np.mean(sb):.4f} | temporal flicker ratio vs native {fl:.3f}")

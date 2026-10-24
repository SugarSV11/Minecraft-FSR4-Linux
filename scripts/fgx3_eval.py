#!/usr/bin/env python3
"""fgx3_eval.py CASE_DIR...: quality of the FG x3 prototype output vs the natively rendered truth frames (same deterministic game state), in a HUD-free ROI.
Reports PSNR / SSIM (luminance, 7x7 box) of: generated (out_t1/out_t2), duplicate nearest real frame (what must not be counted as generation), plain time blend (no motion)."""
import sys, numpy as np
W, H = 1280, 720; ROI = (slice(100, 480), slice(520, 1260))
if sys.argv[1] == '--sheep': ROI = (slice(440, 545), slice(560, 900)); sys.argv.pop(1)   # entity phase: the sheep's upper body, rows free of chat text
def rd(p): return np.fromfile(p, dtype=np.uint8).reshape(H, W, 4)[..., :3].astype(np.float64)
def lum(a): return 0.299 * a[..., 0] + 0.587 * a[..., 1] + 0.114 * a[..., 2]
def box(x, r=3):
    k = 2 * r + 1; c = np.pad(x, ((r + 1, r), (r + 1, r)), mode='edge').cumsum(0).cumsum(1); return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / (k * k)
def ssim(a, b):
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2; ma, mb = box(a), box(b); va = box(a * a) - ma * ma; vb = box(b * b) - mb * mb; cab = box(a * b) - ma * mb
    return float((((2 * ma * mb + c1) * (2 * cab + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2))).mean())
def psnr(a, b): m = ((a - b) ** 2).mean(); return 99.0 if m == 0 else 10 * np.log10(255 ** 2 / m)
res = {n: [] for n in ("generated", "duplicate", "blend")}; ss = {n: [] for n in res}; mres = {n: [] for n in res}   # mres: PSNR over the pixels that change between the real frames (|F1-F0|>8): where motion handling matters
for d in sys.argv[1:]:
    c0, c1 = lum(rd(f"{d}/c0.rgba")), lum(rd(f"{d}/c1.rgba"))
    for j, t in ((1, 1 / 3), (2, 2 / 3)):
        tr = lum(rd(f"{d}/truth{j}.rgba")); g = lum(rd(f"{d}/out_t{j}.rgba"))
        for n, im in (("generated", g), ("duplicate", c0 if t < 0.5 else c1), ("blend", (1 - t) * c0 + t * c1)):
            res[n].append(psnr(im[ROI], tr[ROI])); ss[n].append(ssim(im[ROI], tr[ROI]))
            chg = (np.abs(c1 - c0) > 8)[ROI]
            if chg.sum() > 100: mres[n].append(10 * np.log10(255 ** 2 / max(((im[ROI] - tr[ROI]) ** 2)[chg].mean(), 1e-9)))
print(f"{len(sys.argv) - 1} cases x 2 frames; ROI rows 100-480 cols 520-1260 (no HUD)")
for n in res: print(f"  {n:9s} PSNR {np.mean(res[n]):6.2f} dB (min {np.min(res[n]):.2f})  SSIM {np.mean(ss[n]):.4f}  | changed pixels only: {np.mean(mres[n]) if mres[n] else float('nan'):.2f} dB")

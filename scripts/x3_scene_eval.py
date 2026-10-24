#!/usr/bin/env python3
"""x3_scene_eval.py CASES_DIR [KSPLIT=60]: quality of the replayed FG x3 frames against the TRUE intermediate frames of the same recording.
Reports, for phase A (frames < KSPLIT, static camera) and B (camera moving): PSNR / SSIM (luminance, 7x7 box), PSNR over the pixels that actually change between the real frames,
share of grossly wrong pixels (|err| > 40 levels), and temporal flicker of the presented sequence [real, g1, g2, real] relative to the true sequence. Baselines: duplicate nearest real frame, time blend."""
import sys, os, numpy as np
W, H = 1280, 720; D = sys.argv[1]; split = int(sys.argv[2]) if len(sys.argv) > 2 else 60
def rd(p): a = np.fromfile(p, dtype=np.uint8).reshape(H, W, 4)[..., :3].astype(np.float64); return 0.299 * a[..., 0] + 0.587 * a[..., 1] + 0.114 * a[..., 2]
def box(x, r=3):
    k = 2 * r + 1; c = np.pad(x, ((r + 1, r), (r + 1, r)), mode='edge').cumsum(0).cumsum(1); return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / (k * k)
def ssim(a, b):
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2; ma, mb = box(a), box(b); va = box(a * a) - ma * ma; vb = box(b * b) - mb * mb; cab = box(a * b) - ma * mb
    return float((((2 * ma * mb + c1) * (2 * cab + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2))).mean())
def psnr(a, b): m = ((a - b) ** 2).mean(); return 99.0 if m == 0 else 10 * np.log10(255 ** 2 / m)
cases = [l.strip() for l in open(f"{D}/cases.txt") if l.strip()]
R = {}
for c in cases:
    k = int(os.path.basename(c)[1:])
    if k < split < k + len(open(f"{c}/t.txt").read().split()) + 1: continue   # interval straddles the start of the camera motion: constant-velocity MV scaling is invalid there (test artefact)
    ph = 'A' if k < split else 'B'
    c0, c1 = rd(f"{c}/c0.rgba"), rd(f"{c}/c1.rgba"); t = [float(x) for x in open(f"{c}/t.txt").read().split()]; M = len(t); T = [rd(f"{c}/truth{i}.rgba") for i in range(1, M + 1)]; G = [rd(f"{c}/out_{i}.rgba") for i in range(1, M + 1)]
    chg = np.abs(c1 - c0) > 8
    for name, imgs in (("generated", G), ("duplicate", [c0 if x < .5 else c1 for x in t]), ("blend", [(1 - x) * c0 + x * c1 for x in t])):
        for i in range(M):
            r = R.setdefault((ph, name), {k2: [] for k2 in ("psnr", "ssim", "chg", "gross")}); r["psnr"].append(psnr(imgs[i], T[i])); r["ssim"].append(ssim(imgs[i], T[i]))
            if chg.sum() > 200: r["chg"].append(psnr(imgs[i][chg], T[i][chg]))
            r["gross"].append(float((np.abs(imgs[i] - T[i]) > 40).mean()) * 100)
        seq = [c0] + imgs + [c1]; tseq = [c0] + T + [c1]; R[(ph, name)].setdefault("flick", []).append(np.mean([np.abs(seq[j + 1] - seq[j]).mean() for j in range(M + 1)]) / max(1e-9, np.mean([np.abs(tseq[j + 1] - tseq[j]).mean() for j in range(M + 1)])))
print(f"{len(cases)} cases from {D} (the case straddling the motion start is skipped)")
for ph in "AB":
    if (ph, "generated") not in R: continue
    print(f"phase {ph} ({'static camera' if ph == 'A' else 'moving camera'}):")
    for n in ("generated", "duplicate", "blend"):
        r = R[(ph, n)]; print(f"  {n:9s} PSNR {np.mean(r['psnr']):6.2f} dB (min {np.min(r['psnr']):.2f})  SSIM {np.mean(r['ssim']):.4f} | changed px {np.mean(r['chg']) if r['chg'] else float('nan'):6.2f} dB | gross-error px {np.mean(r['gross']):.3f} % | flicker x{np.mean(r['flick']):.3f}")

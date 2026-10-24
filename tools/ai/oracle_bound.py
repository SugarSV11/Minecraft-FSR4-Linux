#!/usr/bin/env python3
"""oracle_bound.py: upper bounds on what a per-pixel corrector could reach using ONLY frames the pipeline already has.
For each generated frame (x4 cases of the recorded scene suite, moving-camera phase) compare the PSNR (luminance) of
 G = shader output | best-of{G,c0,c1,blend} per pixel (oracle selection) | best-of{G,c0,c1,blend} restricted to pixels where G is grossly wrong.
A selector that cannot pick better than this even with perfect knowledge tells us the information is not in the candidates (a *synthesis* network would be needed)."""
import sys as _sys, os as _os; _sys.path.insert(0, _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), ".")); import ncpaths as NC
import os, numpy as np
E = NC.evidence(9); W, H = 1280, 720
def rd(p): a = np.fromfile(p, dtype=np.uint8).reshape(H, W, 4)[..., :3].astype(np.float32); return a
def lum(a): return 0.299 * a[..., 0] + 0.587 * a[..., 1] + 0.114 * a[..., 2]
def psnr(m): return 99.0 if m == 0 else 10 * np.log10(255 ** 2 / m)
print(f"{'scene':13s} {'n':>3s} | G dB | oracle-select {{G,c0,c1,blend}} dB | gross px %  | share of gross px where NO candidate is within 16 levels")
for sc in ("baseline", "vegetation", "water", "animals", "disocclusion", "transparency", "particles"):
    cases = [l.strip() for l in open(f"{E}/{sc}/cases4/cases.txt") if l.strip()]; cases = [c for c in cases if int(os.path.basename(c)[1:]) >= 60][::2]
    pg, po, gr, none, n = [], [], [], [], 0
    for c in cases:
        c0, c1 = rd(f"{c}/c0.rgba"), rd(f"{c}/c1.rgba"); t = [float(x) for x in open(f"{c}/t.txt").read().split()]
        for i in (1, 2, 3):
            tt = t[i - 1]; G = rd(f"{c}/out_{i}.rgba"); T = rd(f"{c}/truth{i}.rgba"); B = (1 - tt) * c0 + tt * c1
            cand = np.stack([G, c0, c1, B]); err = np.abs(cand - T).max(-1) if False else np.abs(lum(cand) - lum(T)[None])
            best = cand[np.argmin(err, 0), np.arange(H)[:, None], np.arange(W)[None, :]]
            eg = np.abs(lum(G) - lum(T)); big = eg >= 16
            pg.append(psnr(((lum(G) - lum(T)) ** 2).mean())); po.append(psnr(((lum(best) - lum(T)) ** 2).mean())); gr.append(big.mean() * 100)
            none.append(((err.min(0) >= 16) & big).sum() / max(1, big.sum())); n += 1
    print(f"{sc:13s} {n:3d} | {np.mean(pg):5.2f} | {np.mean(po):5.2f} | {np.mean(gr):5.2f} | {100*np.mean(none):5.1f}")

#!/usr/bin/env python3
"""rule_sweep.py: traditional-rule family R(tau): in hole-filled pixels (shader feature F0<0.5) use the temporal blend only where max|C0-C1| < tau (levels; tau=inf = plain hole->blend), else keep the generator.
Per scene: PSNR gain over G (mean per-frame, luminance), SSIM-free; gross% change and ghost% (see docs/NEURAL_ACCEPTANCE.md). Runs on the TRAIN scenes (tau selection) and prints VAL for the chosen ones."""
import sys as _sys, os as _os; _sys.path.insert(0, _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..")); import ncpaths as NC
import sys, os, numpy as np
sys.path.insert(0, os.path.dirname(__file__)); import dataset as D
P = NC.NEURAL; lum = lambda a: 0.299 * a[0] + 0.587 * a[1] + 0.114 * a[2]; ps = lambda m: 10 * np.log10(255 ** 2 / max(m, 1e-9))
taus = [8, 16, 24, 32, 48, 1e9]
def run(scenes, step=3):
    res = {}
    for sc in scenes:
        X, Y, _ = D.load(sc, P); acc = {t: [[], [], []] for t in taus}; base = [[], []]
        for n in range(0, len(X), step):
            x = np.asarray(X[n]).astype(np.float32); y = lum(np.asarray(Y[n]).astype(np.float32)); t = x[21, 0, 0] / 255; bl = (1 - t) * x[9:12] + t * x[12:15]; hole = x[15] < 128; dd = np.abs(x[9:12] - x[12:15]).max(0)
            lg, lb = lum(x[0:3]), lum(bl); base[0].append(ps(np.mean((lg - y) ** 2))); base[1].append(np.mean(np.abs(lg - y) >= 16) * 100)
            for tau in taus:
                m = hole & (dd < tau); lo = np.where(m, lb, lg)
                acc[tau][0].append(ps(np.mean((lo - y) ** 2)) - base[0][-1]); acc[tau][1].append(np.mean(np.abs(lo - y) >= 16) * 100 - base[1][-1])
                acc[tau][2].append(np.mean((np.abs(lo - lb) < 6) & (np.abs(y - lb) >= 24) & (np.abs(lo - y) >= 16)) * 100 - np.mean((np.abs(lg - lb) < 6) & (np.abs(y - lb) >= 24) & (np.abs(lg - y) >= 16)) * 100)
        res[sc] = {t: [np.mean(v) for v in acc[t]] for t in taus}
    return res
for name in ("train", "val"):
    r = run(D.SPLIT[name]); print(f"== {name}: per tau: mean dPSNR / worst-scene dPSNR / max d-gross% / max d-ghost% (percentage points, over scenes)")
    for t in taus: v = np.array([r[s][t] for s in r]); print(f"  tau={t:>5}: {v[:,0].mean():+.3f} / {v[:,0].min():+.3f} / {v[:,1].max():+.3f} / {v[:,2].max():+.3f}")

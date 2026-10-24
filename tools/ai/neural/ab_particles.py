#!/usr/bin/env python3
"""ab_particles.py SCENE...: same recording, generator fed planes WITHOUT (A) / WITH (B) particle vectors. Particle pixels = where the motion-vector feature differs between A and B. Reports error of G_A vs G_B against the truth inside / outside that mask (and the mask's share)."""
import sys as _sys, os as _os; _sys.path.insert(0, _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..")); import ncpaths as NC
import sys, os, numpy as np
sys.path.insert(0, os.path.dirname(__file__)); import dataset as D
from scipy import ndimage as ndi
d = NC.NEURAL
for sc in sys.argv[1:]:
    XA, YA, _ = D.load(sc + "_A", d); XB, YB, _ = D.load(sc + "_B", d); acc = {k: [] for k in ["share", "mae_pA", "mae_pB", "mae_oA", "mae_oB", "gross_pA", "gross_pB", "gross_oA", "gross_oB", "mse_pA", "mse_pB"]}
    for n in range(len(XA)):
        a, b, y = np.asarray(XA[n]).astype(np.float32), np.asarray(XB[n]).astype(np.float32), np.asarray(YA[n]).astype(np.float32)
        pm = ndi.binary_dilation(np.abs(a[18:20] - b[18:20]).max(0) > 0.5, iterations=2)
        if pm.sum() < 200: continue
        ly = 0.299 * y[0] + 0.587 * y[1] + 0.114 * y[2]; la = 0.299 * a[0] + 0.587 * a[1] + 0.114 * a[2]; lb = 0.299 * b[0] + 0.587 * b[1] + 0.114 * b[2]
        ea, eb = np.abs(la - ly), np.abs(lb - ly); om = ~pm
        acc["share"].append(100 * pm.mean()); acc["mae_pA"].append(ea[pm].mean()); acc["mae_pB"].append(eb[pm].mean()); acc["mae_oA"].append(ea[om].mean()); acc["mae_oB"].append(eb[om].mean())
        acc["gross_pA"].append(100 * (ea[pm] >= 16).mean()); acc["gross_pB"].append(100 * (eb[pm] >= 16).mean()); acc["gross_oA"].append(100 * (ea[om] >= 16).mean()); acc["gross_oB"].append(100 * (eb[om] >= 16).mean())
        acc["mse_pA"].append(((la - ly)[pm] ** 2).mean()); acc["mse_pB"].append(((lb - ly)[pm] ** 2).mean())
    m = {k: (np.mean(v) if v else float("nan")) for k, v in acc.items()}
    print(f"{sc:10s} frames_with_particles={len(acc['share'])}/{len(XA)} particle share {m['share']:.2f}% | in-particle MAE A {m['mae_pA']:.2f} B {m['mae_pB']:.2f} gross% A {m['gross_pA']:.1f} B {m['gross_pB']:.1f} | outside MAE A {m['mae_oA']:.3f} B {m['mae_oB']:.3f} gross% A {m['gross_oA']:.2f} B {m['gross_oB']:.2f}")

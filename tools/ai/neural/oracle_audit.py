#!/usr/bin/env python3
"""oracle_audit.py: dataset audit + upper bound of ANY per-pixel selector over {G,A,B,C0,C1,blend}: PSNR (luminance) if each pixel took the candidate closest to the truth (oracle),
overall and inside hole-filled / flagged / fast pixels. Also the share of pixels where G is grossly wrong (>=16 levels) and where NO candidate is within 16 levels (information absent)."""
import sys as _sys, os as _os; _sys.path.insert(0, _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..")); import ncpaths as NC
import sys, os, numpy as np
sys.path.insert(0, os.path.dirname(__file__)); import dataset as D
P = NC.NEURAL; lum = lambda a: 0.299 * a[0] + 0.587 * a[1] + 0.114 * a[2]
ps = lambda m: 99.0 if m <= 0 else 10 * np.log10(255 ** 2 / m)
for sc in sorted(set(sum(D.SPLIT.values(), []))):
    X, Y, meta = D.load(sc, P); acc = {k: [] for k in ["g", "or", "gor_hole", "gg_hole", "gross", "none", "hole"]}
    for n in range(0, len(X), 3):
        x = np.asarray(X[n]).astype(np.float32); y = np.asarray(Y[n]).astype(np.float32); cand = [x[0:3], x[3:6], x[6:9], x[9:12], x[12:15], (1 - x[21, 0, 0] / 255) * x[9:12] + x[21, 0, 0] / 255 * x[12:15]]
        err = np.stack([np.abs(lum(c) - lum(y)) for c in cand]); e0 = err[0]; eo = err.min(0); hole = x[15] < 128
        acc["g"].append(np.mean(e0 ** 2)); acc["or"].append(np.mean(eo ** 2)); acc["gross"].append((e0 >= 16).mean() * 100); acc["none"].append((eo >= 16).mean() * 100); acc["hole"].append(hole.mean() * 100)
        if hole.sum() > 50: acc["gg_hole"].append(np.mean(e0[hole] ** 2)); acc["gor_hole"].append(np.mean(eo[hole] ** 2))
    m = lambda k: np.mean(acc[k]) if acc[k] else float("nan")
    print(f"{sc:13s} frames {len(X):3d} ({[k for k,v in D.SPLIT.items() if sc in v][0]:5s}) | G {ps(m('g')):.2f} dB, oracle-select {ps(m('or')):.2f} dB | grossly wrong {m('gross'):.2f}%, no candidate within 16 lv {m('none'):.2f}% | hole {m('hole'):.2f}% of px: G {ps(m('gg_hole')):.1f} dB -> oracle {ps(m('gor_hole')):.1f} dB")

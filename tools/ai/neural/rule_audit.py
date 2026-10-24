#!/usr/bin/env python3
"""rule_audit.py: per pixel class (hole-filled / depth-inconsistent / ok) and per scene, the MSE (luminance, 8-bit levels^2, summed over the class's pixels / all pixels = contribution to full-frame MSE) of each FIXED candidate policy
{G, A, B, C0, C1, blend} and of the oracle. Tells whether a trivial rule (e.g. 'in holes use C1') already beats the generator. Only TRAIN+VAL scenes (TEST is sealed)."""
import sys as _sys, os as _os; _sys.path.insert(0, _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..")); import ncpaths as NC
import sys, os, numpy as np
sys.path.insert(0, os.path.dirname(__file__)); import dataset as D
P = NC.NEURAL; lum = lambda a: 0.299 * a[0] + 0.587 * a[1] + 0.114 * a[2]
names = ["G", "A", "B", "C0", "C1", "blend", "oracle"]; tot = {c: np.zeros(7) for c in ["hole", "incons", "ok"]}; cnt = {c: 0.0 for c in tot}; npx = 0
for sc in D.SPLIT["train"] + D.SPLIT["val"]:
    X, Y, _ = D.load(sc, P); loc = {c: np.zeros(7) for c in tot}; n_ = 0
    for n in range(0, len(X), 3):
        x = np.asarray(X[n]).astype(np.float32); y = lum(np.asarray(Y[n]).astype(np.float32)); t = x[21, 0, 0] / 255; cand = [x[0:3], x[3:6], x[6:9], x[9:12], x[12:15], (1 - t) * x[9:12] + t * x[12:15]]
        e = np.stack([(lum(c) - y) ** 2 for c in cand]); e = np.concatenate([e, np.sqrt(e).min(0, keepdims=True) ** 2]); hole = x[15] < 128; inc = (x[15] >= 128) & (x[16] < 128); ok = ~hole & ~inc
        for c, m in (("hole", hole), ("incons", inc), ("ok", ok)): loc[c] += e[:, m].sum(1) / (e.shape[1] * 0 + x.shape[1] * x.shape[2]) if False else e[:, m].sum(1)
        n_ += x.shape[1] * x.shape[2]
    for c in tot: tot[c] += loc[c]
    npx += n_
    print(f"{sc:13s} " + " | ".join(f"{c}: " + " ".join(f"{names[i]}={loc[c][i]/n_:.1f}" for i in (0, 4, 5, 6)) for c in tot))
print("ALL SCENES, contribution to full-frame MSE (levels^2):")
for c in tot: print(f"  {c:7s} " + " ".join(f"{names[i]}={tot[c][i]/npx:.2f}" for i in range(7)))

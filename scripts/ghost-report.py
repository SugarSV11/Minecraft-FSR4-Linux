#!/usr/bin/env python3
"""ghost-report.py DIR_NATIVE DIR_MODE...: ghost residue left where the sheep just was.
Mask = pixels bright in the converged frame of the previous step (g{i-1}_099) and dark in the converged frame of step i.
t0 = first frame where the sheep is visible at its new place (mean luminance in the new-only area > 200).
Reported: luminance in the vacated area over the frames from t0 (native = 0 ghost), and the number of frames from t0 whose vacated-area
luminance exceeds the mode's own converged value by more than 5 (ghost frames), plus the worst value."""
import sys, numpy as np
from PIL import Image
def load(d, n): return np.asarray(Image.open(f"{d}/{n}.png").convert('L'), dtype=np.float64)
for d in sys.argv[2:]:
    nat = sys.argv[1]; print(f"== {d.split('/')[-1]}")
    for i in (1, 2):
        old = load(nat, f"g{i-1}_099"); new = load(nat, f"g{i}_099")
        mask = (old > 120) & (new < 40); tgt = (new > 120) & (old < 40)
        fr = [load(d, f"g{i}_{k:03d}") for k in range(100)]
        ghost = [f[mask].mean() for f in fr]; tnew = [f[tgt].mean() for f in fr]
        t0 = next((k for k in range(100) if tnew[k] > 200), None)
        if t0 is None: print(f" step {i}: sheep never reached the new place in the burst"); continue
        conv = ghost[99]; after = ghost[t0:]
        bad = sum(1 for g in after if g > conv + 5)
        print(f" step {i}: t0={t0} ghost lum at t0..t0+5 = {[round(g,1) for g in ghost[t0:t0+6]]} converged={conv:.1f}  ghost frames={bad}/{len(after)}  worst={max(after):.1f}")

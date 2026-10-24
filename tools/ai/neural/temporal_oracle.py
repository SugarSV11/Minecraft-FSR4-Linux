#!/usr/bin/env python3
"""temporal_oracle.py: does extra temporal history add information? Oracle (per-pixel best candidate) with the six current candidates vs with two extra UNWARPED real frames P=frame(k-4), N=frame(k+8) (stride 4). Reports PSNR(oracle), share of pixels with no candidate within 16 levels. Scenes: VAL + 2 TRAIN (session-11 recordings with PNG frames)."""
import sys as _sys, os as _os; _sys.path.insert(0, _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..")); import ncpaths as NC
import sys, os, json, numpy as np
from PIL import Image
sys.path.insert(0, os.path.dirname(__file__)); import dataset as D
P = NC.NEURAL; lum = lambda a: 0.299 * a[0] + 0.587 * a[1] + 0.114 * a[2]; ps = lambda m: 10 * np.log10(255 ** 2 / max(m, 1e-9))
def root(sc): return NC.evidence(12) + "/" + sc if os.path.isdir(NC.evidence(12) + "/" + sc) else NC.evidence(11) + "/" + sc
def img(sc, k): return np.asarray(Image.open(f"{root(sc)}/frames/sc_{k:03d}.png").convert("RGB"), dtype=np.float32)[::-1].transpose(2, 0, 1)
for sc in D.SPLIT["val"] + ["occl_b", "turn_a"]:
    X, Y, meta = D.load(sc, P); a = {k: [] for k in ["or6", "or8", "none6", "none8"]}
    for n in range(0, len(X), 4):
        k = int(os.path.basename(meta[n]["case"])[1:]); 
        if k < 4 or k + 8 > 149: continue
        x = np.asarray(X[n]).astype(np.float32); y = lum(np.asarray(Y[n]).astype(np.float32)); t = x[21, 0, 0] / 255
        c = [x[0:3], x[3:6], x[6:9], x[9:12], x[12:15], (1 - t) * x[9:12] + t * x[12:15]]; e6 = np.stack([np.abs(lum(q) - y) for q in c]).min(0)
        e8 = np.minimum(e6, np.minimum(np.abs(lum(img(sc, k - 4)) - y), np.abs(lum(img(sc, k + 8)) - y)))
        a["or6"].append(np.mean(e6 ** 2)); a["or8"].append(np.mean(e8 ** 2)); a["none6"].append(np.mean(e6 >= 16) * 100); a["none8"].append(np.mean(e8 >= 16) * 100)
    m = lambda q: np.mean(a[q]); print(f"{sc:13s} oracle6 {ps(m('or6')):.2f} dB -> oracle8 {ps(m('or8')):.2f} dB | no candidate within 16 lv: {m('none6'):.3f}% -> {m('none8'):.3f}%  (frames {len(a['or6'])})")

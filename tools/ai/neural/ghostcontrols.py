#!/usr/bin/env python3
"""ghostcontrols.py [--scenes a,b] [--max N]: negative controls for ghostmetrics.py. Deliberately degraded versions of the TRUE intermediate frame are scored; each metric must react to the defect it claims to detect.
Controls: perfect (out = truth), noise (Gaussian), blur (sigma 1.5), shift (global 3 px displacement), ghost (truth + 35 % of itself displaced 8 px: double edges), blend (temporal mix of the real frames),
stale (C0 inside the moving region), flicker (+-6 % brightness per frame; only the temporal metrics can see it), dropout (moving region replaced by flat grey: deleted objects), holefill (hole-filled pixels replaced by a blur).
PSNR-matched trio: ghost / blur / noise strengths are bisected until the three have the same PSNR so the table shows what PSNR cannot tell apart."""
import sys as _sys, os as _os; _sys.path.insert(0, _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..")); import ncpaths as NC
import sys, os, json, argparse, numpy as np
sys.path.insert(0, os.path.dirname(__file__)); import dataset as D, ghostmetrics as G
from scipy import ndimage as ndi
ap = argparse.ArgumentParser(); ap.add_argument("--scenes", default="mixed,transparency,particles"); ap.add_argument("--max", type=int, default=12); ap.add_argument("--data", default=NC.NEURAL); ap.add_argument("--json", default="")
a = ap.parse_args(); rng = np.random.default_rng(0)
def shift(x, dx, dy=0): return np.roll(np.roll(x, dx, 1), dy, 0)
def psnr(o, t): m = ((o - t) ** 2).mean(); return 99.0 if m <= 0 else 10 * np.log10(255 ** 2 / m)
def blur(x, s): return ndi.gaussian_filter(x, (s, s, 0))
def ghost(t, al): return (1 - al) * t + al * shift(t, 8)
def noise(t, sg): return np.clip(t + rng.normal(0, sg, t.shape), 0, 255)
def match(fn, truth, target, lo, hi):
    for _ in range(14):
        mid = (lo + hi) / 2; p = psnr(fn(truth, mid), truth)
        if p > target: lo = mid
        else: hi = mid
    return mid
rows = {}
for scene in a.scenes.split(","):
    X, Y, meta = D.load(scene, a.data); idx = list(range(0, len(X), max(1, len(X) // a.max)))
    for n in idx:
        x = np.asarray(X[n]).astype(np.float32); t = np.asarray(Y[n]).astype(np.float32).transpose(1, 2, 0); c0 = x[9:12].transpose(1, 2, 0); c1 = x[12:15].transpose(1, 2, 0); T = x[21, 0, 0] / 255.0; hole = x[15] < 127
        mov = ndi.binary_dilation(np.abs(G.lum(c1) - G.lum(c0)) >= 12, iterations=4)
        ref = psnr(ghost(t, 0.35), t); sgn = match(noise, t, ref, 0.01, 60); bsg = match(lambda tt, s: blur(tt, s), t, ref, 0.05, 6)
        ctl = {"perfect": t, "noise": noise(t, sgn), "blur": blur(t, bsg), "shift3": shift(t, 3), "ghost": ghost(t, 0.35), "blend": (1 - T) * c0 + T * c1,
               "stale": np.where(mov[..., None], c0, t), "dropout": np.where(mov[..., None], 128.0, t), "holefill": np.where(hole[..., None], blur(t, 4), t)}
        for k, o in ctl.items():
            m = G.frame_metrics(o, t, c0, c1, hole); m["psnr"] = psnr(o, t); rows.setdefault(k, []).append(m)
    # flicker needs a sequence: take the frames of the first full case
    cases = {}
    for n, mt in enumerate(meta): cases.setdefault(mt["case"], []).append(n)
    for c, ns in list(cases.items())[:3]:
        if len(ns) < 3: continue
        xs = [np.asarray(X[n]).astype(np.float32) for n in ns]; ts = [np.asarray(Y[n]).astype(np.float32).transpose(1, 2, 0) for n in ns]; c0 = xs[0][9:12].transpose(1, 2, 0); c1 = xs[0][12:15].transpose(1, 2, 0)
        g = [1 + 0.06 * (-1) ** i for i in range(len(ts))]
        for k, outs in (("perfect", ts), ("flicker", [np.clip(tt * gg, 0, 255) for tt, gg in zip(ts, g)]), ("blend", [(1 - xs[i][21, 0, 0] / 255) * c0 + xs[i][21, 0, 0] / 255 * c1 for i in range(len(ts))]), ("ghost", [ghost(tt, 0.35) for tt in ts])):
            sm = G.sequence_metrics(outs, ts, c0, c1); rows.setdefault(k + "_seq", []).append(sm)
cols = ["psnr", "mae", "excess_f", "excess_c", "missing_f", "missing_c", "hf_ratio", "hf_corr", "dupcorr", "dupcorr_mov", "mix%", "stale%", "ms_ssim", "gsim", "mae_mov", "hole_mae"]
print("control      " + " ".join(f"{c[:9]:>9s}" for c in cols)); out = {}
for k, v in rows.items():
    if k.endswith("_seq"): continue
    out[k] = {c: float(np.nanmean([r[c] for r in v])) for c in cols}; print(f"{k:12s} " + " ".join(f"{out[k][c]:9.3f}" for c in cols))
print("sequence controls (tdiff = temporal change error relative to truth; flick = temporal jumpiness ratio)")
for k in ("perfect_seq", "flicker_seq", "blend_seq", "ghost_seq"):
    if k in rows: out[k] = {c: float(np.mean([r[c] for r in rows[k]])) for c in ("tdiff", "flick")}; print(f"{k:12s} tdiff {out[k]['tdiff']:.3f} flick {out[k]['flick']:.3f}")
if a.json: json.dump(out, open(a.json, "w"), indent=1)

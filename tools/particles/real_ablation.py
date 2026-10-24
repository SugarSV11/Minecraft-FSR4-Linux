#!/usr/bin/env python3
"""Ablation of the particle vectors on a REAL recording (x3-scene.sh with AB=1), through the real generator (fgx3_replay), against the true in-between frames.
Variants of the planes of F1 (the vectors of F0->F1 are those of frame k+S scaled to the span, as in scripts/x3_scene_dataset.py):
  A        planes without particle vectors (FSR4_PARTICLE_MV=0 behaviour)
  B        planes as the shipped particle pass writes them (depth + vector)
  B_mv     particle vector, NO particle depth
  B_x0.5 / B_x1.5   particle displacement scaled (vector = cam + s * (B - A))   -> is the scale wrong?
  B_flipy  particle displacement Y inverted                                      -> is the sign wrong?
  B_xonly  only the horizontal particle displacement
Metrics inside the particle mask (pixels where B differs from A in F1, dilated 6 px) and outside it, PSNR-like MAE and gross-error share, plus ghost metrics.
usage: real_ablation.py SCENE_DIR WORKDIR [stride=4] [max_cases=8]"""
import sys, os, subprocess, shutil, numpy as np
from scipy import ndimage as ndi
from PIL import Image
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "ai", "neural")); import ghostmetrics as G
D, WK = sys.argv[1], sys.argv[2]; S = int(sys.argv[3]) if len(sys.argv) > 3 else 4; MAXC = int(sys.argv[4]) if len(sys.argv) > 4 else 8
W, H = Image.open(f"{D}/frames/sc_010.png").size   # the recording window can be narrower than 1280 when KWin places it on the vertical monitor
REPLAY = os.environ.get("REPLAY", "build/cmake/fgx3_replay")
n = len([f for f in os.listdir(f"{D}/frames") if f.endswith(".png")])
img = lambda k: np.asarray(Image.open(f"{D}/frames/sc_{k:03d}.png").convert("RGBA"), dtype=np.uint8)[::-1]
ts = lambda k: int(open(f"{D}/planes/cam_{k:04d}.txt").read().split()[-1])
mvp = lambda p, k: np.fromfile(f"{D}/planes/{p}_{k:04d}.bin", dtype=np.float16).astype(np.float32).reshape(H, W, 2)
dpl = lambda p, k: np.fromfile(f"{D}/planes/{p}_{k:04d}.bin", dtype=np.float32).reshape(H, W)
VARS = ["A", "B", "B_mv", "B_x0.5", "B_x1.5", "B_x6.8", "B_flipy", "B_xonly"]   # B_x6.8 = the old wall-clock estimate in the TICKRATE=1 recording (min(4, 590 ms/50) = 4 ticks vs 0.59 real)
ks = list(range(10, n - S, S)); ks = ks[:: max(1, len(ks) // MAXC)][:MAXC]
agg = {v: [] for v in VARS}; blend = []
for k in ks:
    T = [ts(k + i) for i in range(S + 1)]; span = T[S] - T[0]
    sc = span / (T[S] - T[S - 1]); mA = mvp("ma", k + S) * sc; mB = mvp("m", k + S) * sc
    dA0, dA1, dB0, dB1 = dpl("da", k), dpl("da", k + S), dpl("d", k), dpl("d", k + S)
    pm = (np.abs(mB - mA).sum(-1) > 1e-4) | (dB1 != dA1); pmd = ndi.binary_dilation(pm, iterations=6)
    if pm.sum() < 300: continue
    c0, c1 = img(k), img(k + S); tl = [(T[q] - T[0]) / span for q in range(1, S)]; truths = [img(k + q)[..., :3].astype(np.float32) for q in range(1, S)]
    for v in VARS:
        d0, d1, mv = (dA0, dA1, mA) if v == "A" else (dB0, dB1, mB)
        if v == "B_mv": d0, d1 = dA0, dA1
        if v.startswith("B_x") and v != "B_xonly": mv = mA + float(v[3:]) * (mB - mA)
        if v == "B_flipy": mv = mB.copy(); mv[..., 1] = mA[..., 1] - (mB[..., 1] - mA[..., 1])
        if v == "B_xonly": mv = mA.copy(); mv[..., 0] = mB[..., 0]
        cd = f"{WK}/c"; os.makedirs(cd, exist_ok=True)
        c0.tofile(f"{cd}/c0.rgba"); c1.tofile(f"{cd}/c1.rgba"); d0.tofile(f"{cd}/d0.f32"); d1.tofile(f"{cd}/d1.f32"); mv.astype(np.float32).tofile(f"{cd}/mv1.f32")
        open(f"{cd}/t.txt", "w").write(" ".join(f"{t:.5f}" for t in tl) + "\n")
        subprocess.run([REPLAY, str(W), str(H), cd], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        outs = [np.fromfile(f"{cd}/out_{i+1}.rgba", np.uint8).reshape(H, W, 4)[..., :3].astype(np.float32) for i in range(S - 1)]
        r = {}
        for o, t in zip(outs, truths):
            lo, lt = G.lum(o), G.lum(t); e = np.abs(lo - lt); m = G.frame_metrics(o, t, c0[..., :3].astype(np.float32), c1[..., :3].astype(np.float32))
            for key, val in (("in_mae", e[pmd].mean()), ("in_gross", 100 * (e[pmd] >= 16).mean()), ("out_mae", e[~pmd].mean()), ("in_dup", G.dupcorr(lo, lt, pmd)), ("in_psnr", 10 * np.log10(255 ** 2 / max(1e-9, ((o - t)[pmd] ** 2).mean()))), ("ex_c", m["excess_c"]), ("mix%", m["mix%"])):
                r.setdefault(key, []).append(val)
        agg[v].append({q: float(np.nanmean(x)) for q, x in r.items()})
    shutil.rmtree(f"{WK}/c", ignore_errors=True)
    blend.append(np.mean([(np.abs(G.lum(c0[..., :3].astype(np.float32) * (1 - t) + c1[..., :3].astype(np.float32) * t) - G.lum(tr))[pmd]).mean() for t, tr in zip(tl, truths)]))
    print("done", k, flush=True)
print(f"\n{len(agg['A'])} cases x {S-1} generated frames; particle mask = vector-written pixels dilated 6 px; temporal-blend in_mae {np.mean(blend):.2f}")
print(f"{'variant':9s} in_mae in_gross% in_dupcorr in_psnr | out_mae | excess_c mix%")
for v in VARS:
    a = agg[v]; f = lambda q: float(np.mean([x[q] for x in a]))
    print(f"{v:9s} {f('in_mae'):6.2f} {f('in_gross'):8.1f} {f('in_dup'):10.3f} {f('in_psnr'):7.2f} | {f('out_mae'):7.2f} | {f('ex_c'):8.3f} {f('mix%'):5.2f}")

#!/usr/bin/env python3
"""ghost_eval.py --scenes a,b [--ckpt s12_h1.best.pt ...] [--max N] [--json out]: ghosting / duplication / blur / flicker metrics (ghostmetrics.py) for the generator (G), the time blend (B), the traditional rule R (holes -> temporal blend)
and any checkpoints, against the TRUE intermediate frames. Output = per scene, per candidate: mean over the sampled frames; the sequence metrics use whole cases (all generated frames between two real frames)."""
import sys as _sys, os as _os; _sys.path.insert(0, _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..")); import ncpaths as NC
import sys, os, json, argparse, numpy as np, torch
sys.path.insert(0, os.path.dirname(__file__)); import dataset as D, models as M, ghostmetrics as G
ap = argparse.ArgumentParser(); ap.add_argument("--scenes", required=True); ap.add_argument("--ckpt", nargs="*", default=[]); ap.add_argument("--max", type=int, default=24); ap.add_argument("--data", default=NC.NEURAL); ap.add_argument("--json", default="")
ap.add_argument("--prior", type=float, default=0.0); a = ap.parse_args(); torch.set_num_threads(2)
nets = {}
for c in a.ckpt:
    st = torch.load(c); n = M.make(st["arch"]); n.load_state_dict(st["net"]); n.eval()
    if a.prior: n.prior = a.prior
    nets[os.path.basename(c).replace(".best.pt", "")] = n
def rule(x):
    G_, A, B, C0, C1, Fh, Mm, T = M.split(x); bl = (1 - T) * C0 + T * C1; m = (Fh[:, 0:1] < 0.5).float(); return m * bl + (1 - m) * G_
KEYS = ["psnr", "mae", "excess_f", "excess_c", "missing_f", "hf_ratio", "hf_corr", "dupcorr", "dupcorr_mov", "mix%", "stale%", "ms_ssim", "gsim", "mae_mov", "gross_mov%", "hole_mae", "hole_gross%", "tdiff", "flick"]
res = {}
for scene in a.scenes.split(","):
    X, Y, meta = D.load(scene, a.data); cases = {}
    for n, m in enumerate(meta): cases.setdefault(m["case"], []).append(n)
    ck = list(cases)[::max(1, len(cases) * 3 // a.max)][: max(1, a.max // 3)]
    acc = {}
    for c in ck:
        ns = cases[c]; xs = [torch.from_numpy(np.asarray(X[n]).astype(np.float32) / 255.0)[None] for n in ns]; ts = [np.asarray(Y[n]).astype(np.float32).transpose(1, 2, 0) for n in ns]
        c0 = xs[0][0, 9:12].numpy().transpose(1, 2, 0) * 255; c1 = xs[0][0, 12:15].numpy().transpose(1, 2, 0) * 255; outs = {}
        for k in ("G", "B", "R") + tuple(nets):
            o = []
            for x in xs:
                with torch.no_grad():
                    if k == "G": y = x[:, :3]
                    elif k == "B": y = (1 - x[:, 21:22] ) * x[:, 9:12] + x[:, 21:22] * x[:, 12:15]
                    elif k == "R": y = rule(x)
                    else: y = nets[k](x).clamp(0, 1)
                o.append(y[0].numpy().transpose(1, 2, 0) * 255)
            outs[k] = o
        for k, o in outs.items():
            sm = G.sequence_metrics(o, ts, c0, c1) if len(o) > 1 else {"tdiff": np.nan, "flick": np.nan}
            for i, (oo, tt, x) in enumerate(zip(o, ts, xs)):
                m = G.frame_metrics(oo, tt, c0, c1, x[0, 15].numpy() < 0.5); m["psnr"] = 10 * np.log10(255 ** 2 / max(1e-9, m["mse"])); m.update(sm); acc.setdefault(k, []).append(m)
    res[scene] = {k: {q: float(np.nanmean([m[q] for m in v])) for q in KEYS} for k, v in acc.items()}
    print(f"== {scene} ({len(ck)} cases)"); print(f"{'':6s}" + " ".join(f"{q[:9]:>9s}" for q in KEYS))
    for k, v in res[scene].items(): print(f"{k:6s}" + " ".join(f"{v[q]:9.3f}" for q in KEYS))
    sys.stdout.flush()
if a.json: json.dump(res, open(a.json, "w"), indent=1)

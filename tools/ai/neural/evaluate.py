#!/usr/bin/env python3
"""evaluate.py CHECKPOINT [--split test|val] [--selective] [--save N]: full-frame (1280x720) evaluation against the TRUE intermediate frames, scene by scene, on scenes never used for training (docs/NEURAL_EVALUATION.md).
Reports for the generator alone (G), the plain time blend and the corrected frame: PSNR / SSIM (luminance), grossly-wrong pixel share (>=16 levels), PSNR inside the flagged region / the hole-filled pixels / fast-moving pixels,
temporal flicker of the sequence [C0, g1..g(n-1), C1] relative to the true sequence, and the WORST per-frame change (a mean gain must not hide regressions). --selective: the network output is used only inside flagged 16x16 tiles."""
import sys as _sys, os as _os; _sys.path.insert(0, _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..")); import ncpaths as NC
import sys, os, json, argparse, numpy as np, torch, torch.nn.functional as F
sys.path.insert(0, os.path.dirname(__file__)); import dataset as D, models as M
ap = argparse.ArgumentParser(); ap.add_argument("ckpt"); ap.add_argument("--split", default="test"); ap.add_argument("--selective", action="store_true"); ap.add_argument("--save", type=int, default=0)
ap.add_argument("--data", default=NC.NEURAL); ap.add_argument("--threads", type=int, default=12); ap.add_argument("--max", type=int, default=0); ap.add_argument("--json", default=""); ap.add_argument("--scenes", default=""); ap.add_argument("--prior", type=float, default=0.0); ap.add_argument("--tau", type=float, default=1.0, help="sharpen the selection weights: w <- softmax(tau * log w)"); ap.add_argument("--hard", type=float, default=0.0, help="selection nets: use the arg-max candidate only where its weight exceeds this threshold (else keep G); 0 = soft mix")
a = ap.parse_args(); torch.set_num_threads(a.threads)
class Rule(torch.nn.Module):
    """Traditional baselines (no learning): holeblend = replace the generator's hole-filled pixels by the temporal blend (1-t)C0 + tC1; holeblend2 = also depth-inconsistent pixels."""
    def __init__(s, kind): super().__init__(); s.kind = kind
    def forward(s, x):
        G, A, B, C0, C1, Fh, Mm, T = M.split(x); bl = (1 - T) * C0 + T * C1; m = (Fh[:, 0:1] < 0.5).float()
        if s.kind == "holeblend2": m = torch.maximum(m, ((Fh[:, 0:1] > 0.5) & (Fh[:, 1:2] < 0.5)).float())
        return m * bl + (1 - m) * G
if a.ckpt.startswith("rule:"): net = Rule(a.ckpt[5:])
else: st = torch.load(a.ckpt); net = M.make(st["arch"]); net.load_state_dict(st["net"])
if a.prior: net.prior = a.prior
net.eval()
def box(x, r=3):
    x = x.astype(np.float64); k = 2 * r + 1; c = np.pad(x, ((r + 1, r), (r + 1, r)), mode="edge").cumsum(0).cumsum(1); return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / (k * k)
def ssim(x, y):
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2; ma, mb = box(x), box(y); va = box(x * x) - ma * ma; vb = box(y * y) - mb * mb; cab = box(x * y) - ma * mb
    return float((((2 * ma * mb + c1) * (2 * cab + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2))).mean())
def lum(t): return (0.299 * t[0] + 0.587 * t[1] + 0.114 * t[2]) * 255.0
def psnr(m): return 99.0 if m <= 0 else 10 * np.log10(255 ** 2 / m)
def mse(x, y, m=None):
    d = (x - y) ** 2
    if m is None: return d.mean()
    return d[m].mean() if m.sum() > 50 else np.nan
rows = {}; out_all = {}; saved = 0
for scene in (a.scenes.split(",") if a.scenes else D.SPLIT[a.split]):
    X, Y, meta = D.load(scene, a.data); R = {k: [] for k in ["g", "b", "n"]}; S = {k: [] for k in ["g", "b", "n"]}; gross = {k: [] for k in R}; ghost = {k: [] for k in R}; reg = {k: {"flag": [], "hole": [], "fast": []} for k in R}; seqs = {}; delta = []; cover = []
    for n in range(len(X)) if not a.max else range(0, len(X), max(1, len(X) // a.max)):
        x = torch.from_numpy(np.asarray(X[n]).astype(np.float32) / 255.0)[None]; y = np.asarray(Y[n]).astype(np.float32) / 255.0
        with torch.no_grad():
            if a.hard > 0 and hasattr(net, "forward_w"):
                _, w = net.forward_w(x); G_, A_, B_, C0_, C1_, _, _, T_ = M.split(x); cand = torch.stack([G_, A_, B_, C0_, C1_, (1 - T_) * C0_ + T_ * C1_], 1)
                pm, im = w.max(1); use = (pm > a.hard) & (im != 0); pick = torch.gather(cand, 1, im[:, None, None].expand(-1, 1, 3, -1, -1))[:, 0]; o = torch.where(use[:, None], pick, G_)
            elif a.tau != 1.0 and hasattr(net, "forward_w"):
                _, w = net.forward_w(x); w = torch.softmax(a.tau * torch.log(w + 1e-6), 1); G_, A_, B_, C0_, C1_, _, _, T_ = M.split(x); cand = torch.stack([G_, A_, B_, C0_, C1_, (1 - T_) * C0_ + T_ * C1_], 1)
                o = ((w.unsqueeze(2) * cand).sum(1)).clamp(0, 1)
            else: o = net(x).clamp(0, 1)
            fl = M.flag_mask(x); tf = M.tile_flag(fl, 16, 1); cover.append(float(tf.mean()))
            if a.selective: m = F.interpolate(tf, scale_factor=16, mode="nearest")[..., :D.H, :D.W]; o = m * o + (1 - m) * x[:, :3]
        o = o[0].numpy(); g = x[0, :3].numpy(); T = float(x[0, 21, 0, 0]); blend = (1 - T) * x[0, 9:12].numpy() + T * x[0, 12:15].numpy()
        ly = lum(y); flm = fl[0, 0].numpy() > 0.5; hole = x[0, 15].numpy() < 0.5; fast = (np.abs(x[0, 18:20].numpy() - 0.5).max(0) > 3 / 64)
        for k, im in (("g", g), ("b", blend), ("n", o)):
            li = lum(im); R[k].append(psnr(mse(li, ly))); S[k].append(ssim(li, ly)); gross[k].append(float((np.abs(li - ly) >= 16).mean()) * 100); lb = lum(blend); ghost[k].append(float(((np.abs(li - lb) < 6) & (np.abs(ly - lb) >= 24) & (np.abs(li - ly) >= 16)).mean()) * 100)
            for rn, rm in (("flag", flm), ("hole", hole), ("fast", fast)): reg[k][rn].append(psnr(mse(li, ly, rm)) if rm.sum() > 50 else np.nan)
        delta.append(R["n"][-1] - R["g"][-1]); c = meta[n]["case"]; seqs.setdefault(c, {})[meta[n]["i"]] = (lum(g), lum(o), ly, lum(x[0, 9:12].numpy()), lum(x[0, 12:15].numpy()))
        if a.save and saved < a.save and delta[-1] < 5 and gross["g"][-1] > 1.0 and n % 7 == 0:
            from PIL import Image; err = lambda p: np.clip(np.abs(lum(p) - ly) * 4, 0, 255).astype(np.uint8)
            tile = lambda p: (np.clip(p.transpose(1, 2, 0), 0, 1) * 255).astype(np.uint8)[::-1]
            row1 = np.concatenate([tile(g), tile(o), tile(y)], 1); row2 = np.concatenate([np.stack([err(g)[::-1]] * 3, -1), np.stack([err(o)[::-1]] * 3, -1), np.stack([(flm * 255).astype(np.uint8)[::-1]] * 3, -1)], 1)
            os.makedirs("samples", exist_ok=True); Image.fromarray(np.concatenate([row1, row2], 0)).resize((1920, 720)).save(f"samples/{os.path.basename(a.ckpt)[:-3]}_{scene}_{n:04d}.png"); saved += 1
    fl_ = {}
    for k in ("g", "n"):
        v = []
        for c, s in seqs.items():
            if len(s) < 2: continue
            idx = sorted(s); gen = [s[i][0 if k == "g" else 1] for i in idx]; tru = [s[idx[0]][3]] + [s[i][2] for i in idx] + [s[idx[0]][4]]; gs = [s[idx[0]][3]] + gen + [s[idx[0]][4]]
            v.append(np.mean([np.abs(gs[j + 1] - gs[j]).mean() for j in range(len(gs) - 1)]) / max(1e-9, np.mean([np.abs(tru[j + 1] - tru[j]).mean() for j in range(len(tru) - 1)])))
        fl_[k] = float(np.mean(v)) if v else float("nan")
    f = lambda v: float(np.nanmean(v)); d = np.array(delta)
    res = {"frames": len(delta), "PSNR": {k: f(R[k]) for k in R}, "SSIM": {k: f(S[k]) for k in S}, "gross%": {k: f(gross[k]) for k in gross}, "ghost%": {k: f(ghost[k]) for k in ghost}, "flagged_PSNR": {k: f(reg[k]["flag"]) for k in reg}, "hole_PSNR": {k: f(reg[k]["hole"]) for k in reg},
           "fast_PSNR": {k: f(reg[k]["fast"]) for k in reg}, "flicker_g_n": [fl_["g"], fl_["n"]], "gain_mean": float(d.mean()), "gain_worst": float(d.min()), "frames_worse_0.1dB": int((d < -0.1).sum()), "tile_coverage": float(np.mean(cover))}
    out_all[scene] = res
    print(f"{scene:13s} n={res['frames']:3d} | PSNR G {res['PSNR']['g']:.2f} blend {res['PSNR']['b']:.2f} net {res['PSNR']['n']:.2f} ({res['gain_mean']:+.2f}, worst {res['gain_worst']:+.2f}, worse {res['frames_worse_0.1dB']}) | SSIM {res['SSIM']['g']:.4f}->{res['SSIM']['n']:.4f} | gross% {res['gross%']['g']:.2f}->{res['gross%']['n']:.2f} ghost% {res['ghost%']['g']:.3f}->{res['ghost%']['n']:.3f} | flagged dB {res['flagged_PSNR']['g']:.1f}->{res['flagged_PSNR']['n']:.1f} hole {res['hole_PSNR']['g']:.1f}->{res['hole_PSNR']['n']:.1f} fast {res['fast_PSNR']['g']:.1f}->{res['fast_PSNR']['n']:.1f} | flicker {res['flicker_g_n'][0]:.3f}->{res['flicker_g_n'][1]:.3f} | tiles {100*res['tile_coverage']:.0f}%")
if a.json: json.dump(out_all, open(a.json, "w"), indent=1)

#!/usr/bin/env python3
"""train.py ARCH [--iters N] [--bs B] [--crop S] [--lr LR] [--tag T] [--resume]: trains one corrector on the TRAIN scenes, validates on VAL (early stopping / checkpoint choice) -- TEST scenes are never read here.
Loss (docs/NEURAL_TRAINING.md): L = mean(w * |out-Y|) + 0.5 * mean(|sobel(out)-sobel(Y)|) + 0.2 * mean((1-flag) * |out-G|)
  w = 1 + 3 * [|G-Y| >= 16/255]: pixels where the generator is grossly wrong dominate (they carry 70-99 % of the squared error), reconstruction in L1 (robust, sharper than L2);
  edge term: stops the net from blurring edges (L1 alone averages candidates); keep term: outside the detector's flagged area the output must stay the generator's pixel (locality, no global smoothing)."""
import sys as _sys, os as _os; _sys.path.insert(0, _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..")); import ncpaths as NC
import sys, os, time, json, argparse, numpy as np, torch, torch.nn.functional as F
sys.path.insert(0, os.path.dirname(__file__)); import dataset as D, models as M
ap = argparse.ArgumentParser(); ap.add_argument("arch"); ap.add_argument("--iters", type=int, default=3000); ap.add_argument("--bs", type=int, default=12); ap.add_argument("--crop", type=int, default=96)
ap.add_argument("--lr", type=float, default=2e-3); ap.add_argument("--tag", default=""); ap.add_argument("--resume", action="store_true"); ap.add_argument("--data", default=NC.NEURAL); ap.add_argument("--threads", type=int, default=12)
ap.add_argument("--val_every", type=int, default=500); ap.add_argument("--patience", type=int, default=3); ap.add_argument("--seed", type=int, default=0); ap.add_argument("--ce", type=float, default=1.0); ap.add_argument("--ghost", type=float, default=0.0, help="anti-ghosting term: penalises output within 8 levels of the temporal blend where the truth is >=24 levels away from it (docs/NEURAL_ACCEPTANCE.md proxy)"); ap.add_argument("--bias", type=float, default=0.7, help="probability of the importance-sampled (most flagged of 4) crop; 0 = uniform crops, i.e. the full-frame statistics seen at inference"); ap.add_argument("--device", default="cpu", help="cpu | cuda (ROCm build in ~/neuralvenv)"); ap.add_argument("--hinge", type=float, default=0.0, help="weight of the non-regression term mean(relu(|out-Y| - |G-Y|)): punishes only pixels made WORSE than the generator"); ap.add_argument("--ent", type=float, default=0.0, help="entropy of the selection weights (sharper, less blur from mixing misaligned candidates)"); ap.add_argument("--ghinge", type=float, default=0.0, help="penalty on pixels pushed above 16 levels of error that the generator had below"); ap.add_argument("--loss", default="l1", help="l1 (median regression: collapses to identity, see docs/NEURAL_TRAINING.md) | mse")
a = ap.parse_args(); torch.set_num_threads(a.threads); torch.manual_seed(a.seed); np.random.seed(a.seed)
tag = a.tag or a.arch; os.makedirs("runs", exist_ok=True); ck = f"runs/{tag}.pt"; logf = open(f"runs/{tag}.log", "a")
def log(*s):
    m = " ".join(str(x) for x in s); print(m, flush=True); logf.write(m + "\n"); logf.flush()
tr = [D.load(s, a.data) for s in D.SPLIT["train"]]; va = [D.load(s, a.data) for s in D.SPLIT["val"]]
dev = torch.device(a.device); net = M.make(a.arch).to(dev); opt = torch.optim.Adam(net.parameters(), a.lr); sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, a.iters); it0 = 0; best = 1e9; bad = 0
if a.resume and os.path.exists(ck):
    st = torch.load(ck, map_location=dev); net.load_state_dict(st["net"]); opt.load_state_dict(st["opt"]); it0 = st["it"]; best = st["best"]; log(f"resumed at {it0}")
log(f"arch={a.arch} params={sum(p.numel() for p in net.parameters())} GMAC/1080p(all pixels)={M.macs_1080p(net)/1e9:.2f} train frames={sum(len(t[0]) for t in tr)} val frames={sum(len(v[0]) for v in va)} args={vars(a)}")
idx_all = [(si, n) for si, t in enumerate(tr) for n in range(len(t[0]))]
def sample(sets, idx_pool, bs, crop, bias=0.7):
    xs, ys = [], []
    for _ in range(bs):
        si, n = idx_pool[np.random.randint(len(idx_pool))]; X, Y, _ = sets[si]
        # importance sampling: try a few random crops, keep the one with most flagged pixels with probability `bias`
        best_c, best_f = None, -1
        for _t in range(4 if np.random.rand() < bias else 1):
            y0 = np.random.randint(0, D.H - crop); x0 = np.random.randint(0, D.W - crop); p = X[n, 15:18, y0:y0 + crop, x0:x0 + crop]; f = int((p[0] < 128).sum() + ((p[0] >= 128) & (p[1] < 128)).sum())
            if f > best_f: best_c, best_f = (y0, x0), f
        y0, x0 = best_c; xs.append(np.asarray(X[n, :, y0:y0 + crop, x0:x0 + crop])); ys.append(np.asarray(Y[n, :, y0:y0 + crop, x0:x0 + crop]))
    x = torch.from_numpy(np.stack(xs)).float().to(dev) / 255.0; y = torch.from_numpy(np.stack(ys)).float().to(dev) / 255.0
    if np.random.rand() < 0.5:   # horizontal flip: mv x channel mirrored (0.5 +- d); no vertical flip (gravity / bottom-up convention)
        x = x.flip(3); y = y.flip(3); x[:, 18] = 1.0 - x[:, 18]
    return x, y
sob = torch.tensor([[-1., 0, 1], [-2, 0, 2], [-1, 0, 1]]).view(1, 1, 3, 3).repeat(3, 1, 1, 1).to(dev)
def edges(t): gx = F.conv2d(t, sob, padding=1, groups=3); gy = F.conv2d(t, sob.transpose(2, 3), padding=1, groups=3); return gx.abs() + gy.abs()
def cand_labels(x, y):
    if hasattr(net, "cands"): cs = net.cands(x).unbind(1)
    else:
        G, A, B, C0, C1 = x[:, 0:3], x[:, 3:6], x[:, 6:9], x[:, 9:12], x[:, 12:15]; T = x[:, 21:22]; bl = (1 - T) * C0 + T * C1; cs = (G, A, B, C0, C1, bl)
    err = torch.stack([(c - y).abs().amax(1) for c in cs], 1); best = err.argmin(1); stay = err[:, 0] <= err.min(1).values + 4 / 255
    return torch.where(stay, torch.zeros_like(best), best), err
def loss_fn(out, x, y, w=None):
    G = (x[:, :3] if getattr(net, "nb", False) else M.rule_base(x)) if hasattr(net, "cands") else x[:, :3];   # base image: the hinge / keep terms protect this one (R for the session-12 family, G before)
    gen = x[:, :3]; gross = ((gen - y).abs().amax(1, keepdim=True) >= 16 / 255).float(); wgt = 1 + 3 * gross; fl = M.flag_mask(x)
    ce = 0.0; hg = 0.0
    if w is not None:   # selection networks: cross-entropy towards the candidate that is closest to the truth (L1 through the softmax mix alone has a vanishing gradient, measured: loss stayed flat)
        lab, err = cand_labels(x, y); ce = a.ce * ((1 + 6 * (err[:, 0] >= 12 / 255).float()) * F.nll_loss(torch.log(w + 1e-6), lab, reduction='none')).mean()
    hg = a.hinge * F.relu((out - y).abs() - (G - y).abs()).mean() * 10 if a.hinge > 0 else 0.0   # (bug fixed in session 12: the mse branch used to return before this term, so runs b0/b0g/g2/r2/r3/u1/c1/m* had NO hinge)
    if w is not None and a.ent > 0: hg = hg + a.ent * (-(w * torch.log(w + 1e-6)).sum(1)).mean()
    if a.ghinge > 0: hg = hg + a.ghinge * (F.relu((out - y).abs().amax(1) - 16 / 255) - F.relu((G - y).abs().amax(1) - 16 / 255)).clamp(min=0).mean() * 10
    if a.ghost > 0:
        T = x[:, 21:22]; bl = (1 - T) * x[:, 9:12] + T * x[:, 12:15]; far = ((y - bl).abs().amax(1, keepdim=True) >= 24 / 255).float(); hg = hg + a.ghost * (far * F.relu(8 / 255 - (out - bl).abs().amax(1, keepdim=True)) * 255 / 8).mean() * 10
    if a.loss == "mse": return hg + ce + (wgt * (out - y) ** 2).mean() * 20 + 0.1 * (edges(out) - edges(y)).abs().mean()
    return hg + ce + (wgt * (out - y).abs()).mean() + 0.5 * (edges(out) - edges(y)).abs().mean() + 0.2 * ((1 - fl) * (out - G).abs()).mean()
def lum(t): return 0.299 * t[:, 0] + 0.587 * t[:, 1] + 0.114 * t[:, 2]
def validate():
    """Session 12 selection criterion (fixed before any session-12 result): full 1280x720 frames, every 6th frame of EVERY validation scene; per scene the mean PSNR gain (luminance) of the net over the generator;
    score = mean over scenes of gain + 2 * min(0, worst scene gain): a model that regresses one scene cannot win on the average of the others. Returns (PSNR_G, PSNR_net, -score) -- lower third value is better."""
    net.eval(); gains = []; pg_all = []; pn_all = []
    with torch.no_grad():
        for X, Y, meta in va:
            g = []
            for n in range(0, len(X), 6):
                x = torch.from_numpy(np.asarray(X[n]).astype(np.float32) / 255.0)[None].to(dev); y = torch.from_numpy(np.asarray(Y[n]).astype(np.float32) / 255.0)[None].to(dev)
                o = net(x).clamp(0, 1); mg = ((lum(x[:, :3]) - lum(y)) ** 2).mean().item(); mn = ((lum(o) - lum(y)) ** 2).mean().item()
                pg = 10 * np.log10(1 / max(mg, 1e-9)); pn = 10 * np.log10(1 / max(mn, 1e-9)); g.append(pn - pg); pg_all.append(pg); pn_all.append(pn)
            gains.append(np.mean(g))
    net.train(); gains = np.array(gains); score = gains.mean() + 2 * min(0.0, gains.min()); validate.last = gains
    return float(np.mean(pg_all)), float(np.mean(pn_all)), -float(score)
t0 = time.time(); run = 0.0; net.train()
for it in range(it0, a.iters):
    x, y = sample(tr, idx_all, a.bs, a.crop, a.bias)
    if hasattr(net, 'forward_w'): out, wsel = net.forward_w(x); loss = loss_fn(out, x, y, wsel)
    else: out = net(x); loss = loss_fn(out, x, y)
    opt.zero_grad(); loss.backward(); torch.nn.utils.clip_grad_norm_(net.parameters(), 1.0); opt.step(); sched.step(); run = 0.98 * run + 0.02 * loss.item() if it > it0 else loss.item()
    if (it + 1) % 100 == 0: log(f"it {it+1} loss {run:.5f} ({time.time()-t0:.0f}s)")
    if (it + 1) % a.val_every == 0 or it + 1 == a.iters:
        pg, pn, mse = validate(); log(f"VAL it {it+1}: generator {pg:.2f} dB, corrected {pn:.2f} dB ({pn-pg:+.2f}) | per-scene gain {np.round(validate.last, 2).tolist()} score {-mse:+.3f}")
        if mse < best: best = mse; bad = 0; torch.save({"net": net.state_dict(), "opt": opt.state_dict(), "it": it + 1, "best": best, "arch": a.arch}, ck); torch.save({"net": {k: v.cpu() for k, v in net.state_dict().items()}, "arch": a.arch}, f"runs/{tag}.best.pt"); log("  checkpoint (best val)")
        else:
            bad += 1; torch.save({"net": net.state_dict(), "opt": opt.state_dict(), "it": it + 1, "best": best, "arch": a.arch}, ck)
            if bad >= a.patience: log("early stopping"); break
log("done")

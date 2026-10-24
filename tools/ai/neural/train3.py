#!/usr/bin/env python3
"""train3.py ARCH --tag T [...]: session-13 trainer. Differences from train.py (docs/NEURAL_TRAINING.md): (1) the output is a HARD selection of one REAL candidate per pixel (straight-through arg-max: forward = one-hot, backward = softmax),
so the net cannot average two images (the cause of duplicated smoke); (2) L1 + edge loss, hinge in L1 against the GENERATOR G (per pixel: never worse than G), cross-entropy to the best candidate; (3) validation on full frames with the v2 criteria
(docs/NEURAL_ACCEPTANCE.md): checkpoint score = mean dPSNR + 3 * (-mean dMAE_moving) - 2 * |worst dPSNR| - 5 * mean max(0,dMAE_moving) - 0.5 * mean max(0,dmix%) - 5 * mean max(0, -dHF - 0.02).  TEST scenes are never read."""
import sys as _sys, os as _os; _sys.path.insert(0, _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..")); import ncpaths as NC
import sys, os, time, argparse, numpy as np, torch, torch.nn.functional as F
sys.path.insert(0, os.path.dirname(__file__)); import dataset as D, models as M
ap = argparse.ArgumentParser(); ap.add_argument("arch", default="sbng8"); ap.add_argument("--iters", type=int, default=3000); ap.add_argument("--bs", type=int, default=24); ap.add_argument("--crop", type=int, default=128); ap.add_argument("--lr", type=float, default=1e-3)
ap.add_argument("--tag", required=True); ap.add_argument("--data", default=NC.NEURAL); ap.add_argument("--threads", type=int, default=4); ap.add_argument("--val_every", type=int, default=500); ap.add_argument("--patience", type=int, default=5)
ap.add_argument("--seed", type=int, default=0); ap.add_argument("--device", default="cuda"); ap.add_argument("--ce", type=float, default=0.3); ap.add_argument("--hinge", type=float, default=1.0); ap.add_argument("--edge", type=float, default=0.3); ap.add_argument("--hard", type=int, default=1)
ap.add_argument("--stay_margin", type=float, default=3.0, help="levels: G stays the label unless another candidate is better by more than this"); ap.add_argument("--mixpen", type=float, default=0.0, help="penalty on outputs that equal the temporal blend where the truth is not")
a = ap.parse_args(); torch.set_num_threads(a.threads); torch.manual_seed(a.seed); np.random.seed(a.seed)
ck = f"runs/{a.tag}.pt"; logf = open(f"runs/{a.tag}.log", "a")
def log(*s):
    m = " ".join(str(x) for x in s); print(m, flush=True); logf.write(m + "\n"); logf.flush()
tr = [D.load(s, a.data) for s in D.SPLIT3["train"]]; va = [D.load(s, a.data) for s in D.SPLIT3["val"]]
dev = torch.device(a.device); net = M.make(a.arch).to(dev); opt = torch.optim.Adam(net.parameters(), a.lr); sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, a.iters); best = -1e9; bad = 0
log(f"arch={a.arch} params={sum(p.numel() for p in net.parameters())} train frames={sum(len(t[0]) for t in tr)} val frames={sum(len(v[0]) for v in va)} val scenes={len(va)} args={vars(a)}")
idx_all = [(si, n) for si, t in enumerate(tr) for n in range(len(t[0]))]
def sample(bs, crop):
    xs, ys = [], []
    for _ in range(bs):
        si, n = idx_all[np.random.randint(len(idx_all))]; X, Y, _ = tr[si]; y0 = np.random.randint(0, D.H - crop); x0 = np.random.randint(0, D.W - crop)
        xs.append(np.asarray(X[n, :, y0:y0 + crop, x0:x0 + crop])); ys.append(np.asarray(Y[n, :, y0:y0 + crop, x0:x0 + crop]))
    x = torch.from_numpy(np.stack(xs)).float().to(dev) / 255.0; y = torch.from_numpy(np.stack(ys)).float().to(dev) / 255.0
    if np.random.rand() < 0.5: x = x.flip(3); y = y.flip(3); x[:, 18] = 1.0 - x[:, 18]
    return x, y
sob = torch.tensor([[-1., 0, 1], [-2, 0, 2], [-1, 0, 1]]).view(1, 1, 3, 3).repeat(3, 1, 1, 1).to(dev)
def edges(t): return F.conv2d(t, sob, padding=1, groups=3).abs() + F.conv2d(t, sob.transpose(2, 3), padding=1, groups=3).abs()
def lum(t): return 0.299 * t[:, 0] + 0.587 * t[:, 1] + 0.114 * t[:, 2]
def select(x, hard):
    """returns output image, weights, candidate stack. Candidate 0 is G (nb family)."""
    out_soft, w = net.forward_w(x); cs = net.cands(x)
    if hard:
        oh = F.one_hot(w.argmax(1), w.shape[1]).permute(0, 3, 1, 2).to(w.dtype); st = oh + w - w.detach(); out = (st.unsqueeze(2) * cs).sum(1)
        if getattr(net, "gate", False): m = F.max_pool2d(M.flag_mask(x), 17, 1, 8); out = m * out + (1 - m) * x[:, :3]
    else: out = out_soft
    return out, w, cs
def loss_fn(x, y):
    out, w, cs = select(x, bool(a.hard)); G = x[:, :3]
    err = (cs - y.unsqueeze(1)).abs().amax(2); best = err.argmin(1); stay = err[:, 0] <= err.min(1).values + a.stay_margin / 255; lab = torch.where(stay, torch.zeros_like(best), best)
    ce = (( 1 + 6 * (err[:, 0] >= 12 / 255).float()) * F.nll_loss(torch.log(w + 1e-6), lab, reduction="none")).mean()
    gross = ((G - y).abs().amax(1, keepdim=True) >= 16 / 255).float()
    l = ((1 + 3 * gross) * (out - y).abs()).mean() + a.edge * (edges(out) - edges(y)).abs().mean() + a.ce * ce + a.hinge * 10 * F.relu((out - y).abs() - (G - y).abs()).mean()
    if a.mixpen > 0:
        T = x[:, 21:22]; bl = (1 - T) * x[:, 9:12] + T * x[:, 12:15]; far = ((y - bl).abs().amax(1, keepdim=True) >= 16 / 255).float(); sep = ((x[:, 9:12] - x[:, 12:15]).abs().amax(1, keepdim=True) >= 24 / 255).float()
        l = l + a.mixpen * (far * sep * F.relu(1 - (out - bl).abs().amax(1, keepdim=True) * 255 / 4)).mean() * 10
    return l
def hfe(l): return (l - F.avg_pool2d(F.pad(l[:, None], (2, 2, 2, 2), mode="replicate"), 5, 1)[:, 0]).abs().mean()
def frame_stats(o, g, y, c0, c1):
    lo, lg, ly, l0, l1 = lum(o), lum(g), lum(y), lum(c0), lum(c1)
    mov = F.max_pool2d((((l1 - l0).abs() * 255) >= 12).float()[:, None], 9, 1, 4)[:, 0] > 0.5; res = {}
    for k, im in (("g", lg), ("n", lo)):
        e = (im - ly).abs() * 255; res["psnr_" + k] = 10 * torch.log10(1 / ((im - ly) ** 2).mean().clamp_min(1e-9)).item(); res["mae_" + k] = e.mean().item(); res["maem_" + k] = e[mov].mean().item() if mov.sum() > 50 else float("nan")
        res["hf_" + k] = (hfe(im) / hfe(ly).clamp_min(1e-9)).item()
        sep = ((l0 - l1).abs() * 255 >= 24); mix = torch.zeros_like(sep)
        for w_ in (0.2, 0.35, 0.5, 0.65, 0.8): mx = w_ * l0 + (1 - w_) * l1; mix |= ((im - mx).abs() * 255 < 4) & ((ly - mx).abs() * 255 >= 16)
        res["mix_" + k] = 100 * (sep & mix).float().mean().item()
    return res
def validate():
    net.eval(); per = []
    with torch.no_grad():
        for X, Y, meta in va:
            acc = []
            for n in range(0, len(X), 6):
                x = torch.from_numpy(np.asarray(X[n]).astype(np.float32) / 255.0)[None].to(dev); y = torch.from_numpy(np.asarray(Y[n]).astype(np.float32) / 255.0)[None].to(dev)
                o = select(x, True)[0].clamp(0, 1); acc.append(frame_stats(o, x[:, :3], y, x[:, 9:12], x[:, 12:15]))
            m = {k: float(np.nanmean([r[k] for r in acc])) for k in acc[0]}; m["worst"] = float(min(r["psnr_n"] - r["psnr_g"] for r in acc)); per.append(m)
    net.train(); dp = np.array([m["psnr_n"] - m["psnr_g"] for m in per]); dm = np.array([m["maem_n"] - m["maem_g"] for m in per]); dx = np.array([m["mix_n"] - m["mix_g"] for m in per]); dh = np.array([m["hf_n"] - m["hf_g"] for m in per])
    score = dp.mean() + 3 * (-dm.mean()) - 2 * abs(min(0, dp.min())) - 5 * np.maximum(0, dm).mean() - 0.5 * np.maximum(0, dx).mean() - 5 * np.maximum(0, -dh - 0.02).mean()
    validate.last = (dp, dm, dx, dh); return float(score)
t0 = time.time(); run = 0.0; net.train()
for it in range(a.iters):
    x, y = sample(a.bs, a.crop); loss = loss_fn(x, y); opt.zero_grad(); loss.backward(); torch.nn.utils.clip_grad_norm_(net.parameters(), 1.0); opt.step(); sched.step(); run = 0.98 * run + 0.02 * loss.item() if it else loss.item()
    if (it + 1) % 100 == 0: log(f"it {it+1} loss {run:.5f} ({time.time()-t0:.0f}s)")
    if (it + 1) % a.val_every == 0 or it + 1 == a.iters:
        sc = validate(); dp, dm, dx, dh = validate.last
        log(f"VAL it {it+1}: score {sc:+.3f} | dPSNR mean {dp.mean():+.2f} min {dp.min():+.2f} | dMAEmov mean {dm.mean():+.3f} max {dm.max():+.3f} | dmix% mean {dx.mean():+.2f} max {dx.max():+.2f} | dHF mean {dh.mean():+.3f} min {dh.min():+.3f}")
        if sc > best: best = sc; bad = 0; torch.save({"net": {k: v.cpu() for k, v in net.state_dict().items()}, "arch": a.arch}, f"runs/{a.tag}.best.pt"); log("  checkpoint (best val)")
        else:
            bad += 1
            if bad >= a.patience: log("early stopping"); break
log("done")

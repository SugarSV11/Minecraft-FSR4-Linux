#!/usr/bin/env python3
"""pixel_probe.py: how much information is in the PER-PIXEL features? A 1x1-convolution (per-pixel MLP, no spatial context) selector over {G,A,B,C0,C1,blend} is trained on pixels of the TRAIN scenes and
evaluated on all pixels of the TEST scenes (PSNR in luminance vs the generator). If this already gains, the dataset/features carry usable signal; spatial context can only add to it."""
import sys as _sys, os as _os; _sys.path.insert(0, _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..")); import ncpaths as NC
import sys, os, numpy as np, torch, torch.nn as nn, torch.nn.functional as F
sys.path.insert(0, os.path.dirname(__file__)); import dataset as D
torch.set_num_threads(8); torch.manual_seed(0); np.random.seed(0)
P = NC.NEURAL
def pix(scene, n_per_frame, flagged_bias=0.5, rng=np.random):
    X, Y, _ = D.load(scene, P); xs, ys = [], []
    for n in range(len(X)):
        x = np.asarray(X[n]).astype(np.float32) / 255.0; y = np.asarray(Y[n]).astype(np.float32) / 255.0
        fl = (x[15] < 0.5) | ((x[15] > 0.5) & (x[16] < 0.5)) | (np.abs(x[3:6] - x[6:9]).max(0) > 12 / 255)
        idx_f = np.flatnonzero(fl.ravel()); idx_u = np.flatnonzero(~fl.ravel()); k = n_per_frame
        sel = np.concatenate([rng.choice(idx_f, min(len(idx_f), int(k * flagged_bias)), replace=False) if len(idx_f) else np.zeros(0, int), rng.choice(idx_u, int(k * (1 - flagged_bias)), replace=False)])
        xs.append(x.reshape(22, -1)[:, sel].T); ys.append(y.reshape(3, -1)[:, sel].T)
    return np.concatenate(xs), np.concatenate(ys)
class Sel(nn.Module):
    def __init__(s, h=48):
        super().__init__(); s.f = nn.Sequential(nn.Linear(22, h), nn.ReLU(), nn.Linear(h, h), nn.ReLU(), nn.Linear(h, 6))
        with torch.no_grad(): s.f[-1].bias.zero_(); s.f[-1].bias[0] = 3.0
    def forward(s, x):
        G, A, B, C0, C1 = x[:, 0:3], x[:, 3:6], x[:, 6:9], x[:, 9:12], x[:, 12:15]; T = x[:, 21:22]; bl = (1 - T) * C0 + T * C1
        w = torch.softmax(s.f(x), 1); return (w.unsqueeze(2) * torch.stack([G, A, B, C0, C1, bl], 1)).sum(1), w
tx, ty = [], []
for sc in D.SPLIT["train"]: a, b = pix(sc, 30000); tx.append(a); ty.append(b)
tx = torch.from_numpy(np.concatenate(tx)); ty = torch.from_numpy(np.concatenate(ty)); print("train pixels", len(tx))
net = Sel(); opt = torch.optim.Adam(net.parameters(), 3e-3)
for ep in range(1, 41):
    perm = torch.randperm(len(tx)); tot = 0
    for i in range(0, len(tx), 4096):
        b = perm[i:i + 4096]; o, _ = net(tx[b]); gross = ((tx[b][:, :3] - ty[b]).abs().amax(1, keepdim=True) >= 16 / 255).float(); l = ((1 + 3 * gross) * (o - ty[b]).abs()).mean(); opt.zero_grad(); l.backward(); opt.step(); tot += l.item()
    if ep % 10 == 0: print("epoch", ep, "loss", tot / (len(tx) / 4096))
def lumt(t): return 0.299 * t[:, 0] + 0.587 * t[:, 1] + 0.114 * t[:, 2]
for sc in D.SPLIT["val"] + D.SPLIT["test"]:
    X, Y, _ = D.load(sc, P); mg, mn, mb = [], [], []; usage = np.zeros(6)
    for n in range(0, len(X), 2):
        x = torch.from_numpy(np.asarray(X[n]).astype(np.float32).reshape(22, -1).T / 255.0); y = torch.from_numpy(np.asarray(Y[n]).astype(np.float32).reshape(3, -1).T / 255.0)
        with torch.no_grad(): o, w = net(x)
        bl = (1 - x[:, 21:22]) * x[:, 9:12] + x[:, 21:22] * x[:, 12:15]
        mg.append(((lumt(x[:, :3]) - lumt(y)) ** 2).mean().item() * 255 ** 2); mn.append(((lumt(o) - lumt(y)) ** 2).mean().item() * 255 ** 2); mb.append(((lumt(bl) - lumt(y)) ** 2).mean().item() * 255 ** 2); usage += w.mean(0).numpy()
    p = lambda m: 10 * np.log10(255 ** 2 / np.mean(m))
    print(f"{sc:13s} G {p(mg):.2f} dB | blend {p(mb):.2f} | per-pixel selector {p(mn):.2f} ({p(mn)-p(mg):+.2f}) | mean weights G,A,B,C0,C1,blend = {np.round(usage/ len(mg),2)}")

#!/usr/bin/env python3
"""refine_experiment.py: can a TINY CNN (a few thousand weights) improve the frames our FG x3 shader generates, on held-out scenes, and at what arithmetic cost?

Data = the recorded scene suite of docs/FG_X3.md / VISUAL_QUALITY.md (7 scenes x 49 cases, 1280x720, real frames c0/c1, TRUE intermediate frames truth1/2 at t~1/3, 2/3, and
out_1/2 = what the shader generated). Train scenes and TEST scenes are DISJOINT (the network never sees a test scene). Metric: PSNR / SSIM against the true frame, luminance-based
SSIM like scripts/x3_scene_eval.py; baselines: the shader output itself (what the user gets today) and the plain time blend.
Network: residual refiner  out = G + conv3(relu(conv3(relu(conv3([G, c0, c1, t]))))), C hidden channels. Trained on CPU with PyTorch (a few minutes).
Usage: refine_experiment.py [C=8] [epochs=12] [EVID=~/Escritorio/FSR4-evidence-session9]
"""
import sys as _sys, os as _os; _sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__))); import ncpaths as NC
import sys, os, time, json, numpy as np, torch, torch.nn as nn, torch.nn.functional as F
torch.set_num_threads(8); torch.manual_seed(0); np.random.seed(0)
C = int(sys.argv[1]) if len(sys.argv) > 1 else 8; EPOCHS = int(sys.argv[2]) if len(sys.argv) > 2 else 12; PHASE = os.environ.get('PHASE', 'B'); ITERS = int(os.environ.get('ITERS', '400'))
E = os.path.expanduser(sys.argv[3]) if len(sys.argv) > 3 else NC.evidence(9); W, H = 1280, 720
TRAIN = ["baseline", "vegetation", "water", "animals"]; TEST = ["disocclusion", "transparency", "particles"]
def rd(p): return np.fromfile(p, dtype=np.uint8).reshape(H, W, 4)[..., :3].astype(np.float32) / 255.0
def load(scenes, step=1, phase=None):
    X, Y, G, B, meta = [], [], [], [], []
    for s in scenes:
        cases = [l.strip() for l in open(f"{E}/{s}/cases/cases.txt") if l.strip()]
        if phase == 'B': cases = [c for c in cases if int(os.path.basename(c)[1:]) >= 60]
        elif phase == 'A': cases = [c for c in cases if int(os.path.basename(c)[1:]) < 58]
        cases = cases[::step]
        for c in cases:
            c0, c1 = rd(f"{c}/c0.rgba"), rd(f"{c}/c1.rgba"); t = [float(x) for x in open(f"{c}/t.txt").read().split()]
            for i in (1, 2):
                g = rd(f"{c}/out_{i}.rgba"); y = rd(f"{c}/truth{i}.rgba"); tt = t[i - 1]
                x = np.concatenate([g, c0, c1, np.full((H, W, 1), tt, np.float32)], -1)
                X.append(x.transpose(2, 0, 1).astype(np.float16)); Y.append(y.transpose(2, 0, 1).astype(np.float16)); meta.append((s, os.path.basename(c), i))
    return np.stack(X), np.stack(Y), meta
LAYERS = int(os.environ.get('LAYERS', '3')); LOSS = os.environ.get('LOSS', 'l1')
class Net(nn.Module):
    # LAYERS = number of 3x3 convolutions (3 = the real-time candidate; more layers use growing dilation = the unconstrained "oracle" upper bound)
    def __init__(s, c):
        super().__init__(); dil = [1] + [min(2 ** i, 8) for i in range(LAYERS - 2)] + [1]; ch = [10] + [c] * (LAYERS - 1) + [3]
        s.cv = nn.ModuleList([nn.Conv2d(ch[i], ch[i + 1], 3, padding=dil[i], dilation=dil[i]) for i in range(LAYERS)]); nn.init.zeros_(s.cv[-1].weight); nn.init.zeros_(s.cv[-1].bias)
    def forward(s, x):
        h = x
        for i, l in enumerate(s.cv): h = l(h) if i == len(s.cv) - 1 else F.relu(l(h))
        return x[:, :3] + h
def box(x, r=3):
    k = 2 * r + 1; c = np.pad(x, ((r + 1, r), (r + 1, r)), mode='edge').cumsum(0).cumsum(1); return (c[k:, k:] - c[:-k, k:] - c[k:, :-k] + c[:-k, :-k]) / (k * k)
def lum(a): return (0.299 * a[0] + 0.587 * a[1] + 0.114 * a[2]) * 255.0
def ssim(a, b):
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2; ma, mb = box(a), box(b); va = box(a * a) - ma * ma; vb = box(b * b) - mb * mb; cab = box(a * b) - ma * mb
    return float((((2 * ma * mb + c1) * (2 * cab + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2))).mean())
def psnr(a, b): m = ((a - b) ** 2).mean(); return 99.0 if m == 0 else 10 * np.log10(255 ** 2 / m)
t0 = time.time(); Xtr, Ytr, mtr = load(TRAIN, 2, PHASE); print(f"train frames {len(Xtr)} ({time.time()-t0:.0f}s)")
net = Net(C); params = sum(p.numel() for p in net.parameters()); opt = torch.optim.Adam(net.parameters(), 2e-3); sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, EPOCHS * ITERS)
CROP, BS = 96, 16
for ep in range(EPOCHS):
    tl = 0.0
    for it in range(ITERS):
        idx = np.random.randint(0, len(Xtr), BS); ys = np.random.randint(0, H - CROP, BS); xs = np.random.randint(0, W - CROP, BS)
        xb = torch.stack([torch.from_numpy(Xtr[i][:, y:y + CROP, x:x + CROP].astype(np.float32)) for i, y, x in zip(idx, ys, xs)])
        yb = torch.stack([torch.from_numpy(Ytr[i][:, y:y + CROP, x:x + CROP].astype(np.float32)) for i, y, x in zip(idx, ys, xs)])
        loss = F.l1_loss(net(xb), yb) if LOSS == 'l1' else F.mse_loss(net(xb), yb) * 20; opt.zero_grad(); loss.backward(); opt.step(); sched.step(); tl += loss.item()
    print(f"epoch {ep+1}/{EPOCHS} train L1 {tl/ITERS*255:.3f} levels ({time.time()-t0:.0f}s)")
del Xtr, Ytr
net.eval(); res = {}
for sc in TEST:
    Xte, Yte, mte = load([sc], 2, PHASE); r = {k: [] for k in ("shader_psnr", "net_psnr", "blend_psnr", "shader_ssim", "net_ssim")}
    with torch.no_grad():
        for x, y in zip(Xte, Yte):
            xt = torch.from_numpy(x.astype(np.float32))[None]; out = net(xt)[0].clamp(0, 1).numpy(); g = x[:3].astype(np.float32); yy = y.astype(np.float32); tt = float(x[9, 0, 0])
            blend = (1 - tt) * x[3:6].astype(np.float32) + tt * x[6:9].astype(np.float32)
            r["shader_psnr"].append(psnr(lum(g), lum(yy))); r["net_psnr"].append(psnr(lum(out), lum(yy))); r["blend_psnr"].append(psnr(lum(blend), lum(yy)))
            r["shader_ssim"].append(ssim(lum(g), lum(yy))); r["net_ssim"].append(ssim(lum(out), lum(yy)))
    res[sc] = {k: float(np.mean(v)) for k, v in r.items()}; res[sc]["frames"] = len(Xte)
    print(f"TEST {sc:13s} n={len(Xte):3d} | blend {res[sc]['blend_psnr']:.2f} dB | shader {res[sc]['shader_psnr']:.2f} dB SSIM {res[sc]['shader_ssim']:.4f} | +net {res[sc]['net_psnr']:.2f} dB SSIM {res[sc]['net_ssim']:.4f} | gain {res[sc]['net_psnr']-res[sc]['shader_psnr']:+.2f} dB")
macs = (10 * C * 9 + (LAYERS - 2) * C * C * 9 + C * 3 * 9) * 1920 * 1080
res["_cost"] = {"C": C, "layers": LAYERS, "loss": LOSS, "params": params, "GMAC_per_frame_1080p": macs / 1e9, "GFLOP_per_frame_1080p": 2 * macs / 1e9}
print(f"net C={C}: {params} weights, {2*macs/1e9:.1f} GFLOP per 1080p frame (every generated frame)")
json.dump(res, open(f"refine_C{C}_L{LAYERS}_{LOSS}_{PHASE}.json", "w"), indent=1)

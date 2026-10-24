#!/usr/bin/env python3
"""dump_sample.py CKPT SCENE INDEX OUTDIR [--tau T]: writes one dataset frame in the engine's planar uint8 layout (x.u8, 22x720x1280) and the PyTorch reference output of the deployed model (ref.f32, 3x720x1280, candidate mix with tau)."""
import sys as _sys, os as _os; _sys.path.insert(0, _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..")); import ncpaths as NC
import sys, os, numpy as np, torch
sys.path.insert(0, os.path.dirname(__file__)); import dataset as D, models as M
ck, scene, idx, out = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]; tau = float(sys.argv[sys.argv.index("--tau") + 1]) if "--tau" in sys.argv else 1.0
os.makedirs(out, exist_ok=True); X, Y, meta = D.load(scene, NC.NEURAL); st = torch.load(ck); net = M.make(st["arch"]); net.load_state_dict(st["net"]); net.eval()
x8 = np.asarray(X[idx]); x = torch.from_numpy(x8.astype(np.float32) / 255.0)[None]
with torch.no_grad():
    _, w = net.forward_w(x); w = torch.softmax(tau * torch.log(w + 1e-6), 1); G, A, B, C0, C1, _, _, T = M.split(x); cand = torch.stack([G, A, B, C0, C1, (1 - T) * C0 + T * C1], 1); o = (w.unsqueeze(2) * cand).sum(1)
x8.tofile(f"{out}/x.u8"); o[0].numpy().astype(np.float32).tofile(f"{out}/ref.f32"); np.asarray(Y[idx]).tofile(f"{out}/y.u8"); print("t =", meta[idx]["t"], "dumped", scene, idx)

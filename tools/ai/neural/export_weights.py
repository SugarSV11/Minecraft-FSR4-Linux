#!/usr/bin/env python3
"""export_weights.py CHECKPOINT OUT.bin [--tau T]: exports a 'selres' selection network (4 convolutions 3x3: 22->C, C->C, C->C, C->6) as float32 for the Vulkan engine.
Layout: for each layer: weights [out][in][3][3] row-major, then bias [out]. The sharpening temperature tau is baked into the six logit rows (softmax(tau*logits) == softmax(tau*log softmax(logits))),
the residual head of the PyTorch model (3 extra outputs) is NOT exported: the deployed model is the candidate mix only, exactly what evaluate.py measures with --tau."""
import sys, struct, numpy as np, torch
ck, out = sys.argv[1], sys.argv[2]; tau = float(sys.argv[sys.argv.index("--tau") + 1]) if "--tau" in sys.argv else 1.0
st = torch.load(ck); sd = st["net"]; assert st["arch"].startswith("selres"), "only the non-U-Net selection networks map onto the fused tile kernel"
names = ["body.inp", "body.mid.0", "body.mid.1", "body.out"]; blobs = []; shapes = []
for n in names:
    w = sd[n + ".weight"].numpy().astype(np.float32).copy(); b = sd[n + ".bias"].numpy().astype(np.float32).copy()
    if n == "body.out": w = w[:6] * tau; b = b[:6] * tau
    blobs += [w.ravel(), b]; shapes.append(list(w.shape))
arr = np.concatenate(blobs); open(out, "wb").write(arr.tobytes()); print("layers", shapes, "floats", len(arr), "->", out)

#!/usr/bin/env python3
"""fg-motion-report.py DIR: Frame Generation under continuous motion (scripts/motion-test.sh with FG=true MOTION_GEN=1).
DIR/mo_NNN.png = real frames, DIR/mog_NNN.png = generated frame shown BEFORE real frame NNN. No ground truth exists for the half-step state, so the proxy is
the average of the two neighbouring real frames (exact up to sub-pixel blur for smooth motion): PSNR(gen, avg) for the camera-pan phase (frames 95-179) and the entity phase (9-88),
plus PSNR(gen, real_k) and the same two numbers for the trivial 'duplicate previous real frame' baseline, so the FG gain is visible."""
import sys, numpy as np
from PIL import Image
D = sys.argv[1]
def L(n, k): return np.asarray(Image.open(f"{D}/{n}_{k:03d}.png").convert('L'), dtype=np.float64)
def psnr(a, b): m = ((a - b) ** 2).mean(); return 99.0 if m == 0 else 10 * np.log10(255 ** 2 / m)
for name, rng in (("entity phase", range(10, 88)), ("camera pan", range(96, 178))):
    g, dup, cur = [], [], []
    for k in rng:
        try: G = L('mog', k)
        except FileNotFoundError: continue
        R0, R1 = L('mo', k - 1), L('mo', k); avg = (R0 + R1) / 2
        g.append(psnr(G, avg)); dup.append(psnr(R0, avg)); cur.append(psnr(G, R1))
    if g: print(f"{name}: {len(g)} gen frames | gen vs mid-proxy {np.mean(g):.2f} dB (min {np.min(g):.2f}) | duplicate-prev vs mid-proxy {np.mean(dup):.2f} dB | gen vs real_k {np.mean(cur):.2f} dB")

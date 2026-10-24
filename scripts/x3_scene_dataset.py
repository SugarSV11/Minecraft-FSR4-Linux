#!/usr/bin/env python3
"""x3_scene_dataset.py SCENE_DIR OUT_DIR [STRIDE=3]: builds replay cases (F0=k, F1=k+STRIDE, truth k+1..k+STRIDE-1; STRIDE=3 -> x3, 4 -> x4) from a dense recording of scripts/x3-scene.sh.
Colour PNGs are flipped to the game's bottom-up orientation (planes already are). t1/t2 = real time position of the truth frames from the per-frame timestamps;
the motion vectors of frame k+3 (relative to k+2) are scaled to span k..k+3 (constant-velocity assumption, exact for the camera paths used)."""
import sys, os, re, numpy as np
from PIL import Image
D, O = sys.argv[1], sys.argv[2]; PV = os.environ.get('PLANE_VARIANT', '');   # PLANE_VARIANT=a: planes WITHOUT particle vectors (da_/ma_, recorded with FSR4_PARTICLE_AB=1); default: d_/m_ as recorded
stride = int(sys.argv[3]) if len(sys.argv) > 3 else 3; W, H = 1280, 720
n = len([f for f in os.listdir(f"{D}/frames") if f.endswith('.png')])
def ts(k): return int(open(f"{D}/planes/cam_{k:04d}.txt").read().split()[-1])
def img(k): return np.asarray(Image.open(f"{D}/frames/sc_{k:03d}.png").convert('RGBA'), dtype=np.uint8)[::-1]
cases = []
for k in range(2, n - stride, stride):
    T = [ts(k + i) for i in range(stride + 1)]; span = T[stride] - T[0]
    if span <= 0 or T[stride] - T[stride - 1] <= 0: continue
    c = f"{O}/k{k:03d}"; os.makedirs(c, exist_ok=True)
    img(k).tofile(f"{c}/c0.rgba"); img(k + stride).tofile(f"{c}/c1.rgba")
    for q in range(1, stride): img(k + q).tofile(f"{c}/truth{q}.rgba")
    for nm, kk in (("d0", k), ("d1", k + stride)): np.fromfile(f"{D}/planes/d{PV}_{kk:04d}.bin", dtype=np.float32).tofile(f"{c}/{nm}.f32")
    mv = np.fromfile(f"{D}/planes/m{PV}_{k+stride:04d}.bin", dtype=np.float16).astype(np.float32) * (span / (T[stride] - T[stride - 1])); mv.tofile(f"{c}/mv1.f32")
    open(f"{c}/t.txt", "w").write(" ".join(f"{(T[q]-T[0])/span:.5f}" for q in range(1, stride)) + "\n"); cases.append(c)
print(len(cases), "cases"); open(f"{O}/cases.txt", "w").write("\n".join(cases) + "\n")

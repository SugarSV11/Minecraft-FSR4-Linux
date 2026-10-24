#!/usr/bin/env python3
"""fgx3_dataset.py DATA_DIR K OUT_DIR: builds one FG x3 test case from the dense recording of scripts/motion-test.sh (FG=true, ENTITY MV on, FSR4_MOTION_PLANES set; truth frames at 1/3 of the real step).
Real frames are F0 = frame K and F1 = frame K+3; truths are K+1 and K+2. Everything is converted to UPRIGHT image convention (the game's planes are bottom-up): rows flipped, MV y negated.
The game's MV is relative to the previous TRUTH frame (1/3 of the interval) and is scaled x3 to span F0->F1 (valid because the test motion is constant-velocity; a real integration receives the full-interval MV).
Writes c0.rgba c1.rgba d0.f32 d1.f32 mv1.f32 (+ truth1.rgba truth2.rgba for the evaluator)."""
import sys, numpy as np
from PIL import Image
D, K, O = sys.argv[1], int(sys.argv[2]), sys.argv[3]; W, H = 1280, 720
import os; os.makedirs(O, exist_ok=True)
def img(k): return np.asarray(Image.open(f"{D}/frames/mo_{k:03d}.png").convert('RGBA'), dtype=np.uint8)
def depth(k): return np.fromfile(f"{D}/planes/d_{k:04d}.bin", dtype=np.float32).reshape(H, W)[::-1]
def mv(k): m = np.fromfile(f"{D}/planes/m_{k:04d}.bin", dtype=np.float16).reshape(H, W, 2).astype(np.float32)[::-1].copy(); m[..., 1] *= -1; return m
img(K).tofile(f"{O}/c0.rgba"); img(K + 3).tofile(f"{O}/c1.rgba"); depth(K).astype(np.float32).tofile(f"{O}/d0.f32"); depth(K + 3).astype(np.float32).tofile(f"{O}/d1.f32")
# MV over the whole interval = sum of the three per-truth-frame MVs at the same pixel (exact for constant velocity at constant depth; see header)
(3 * mv(K + 3)).astype(np.float32).tofile(f"{O}/mv1.f32"); img(K + 1).tofile(f"{O}/truth1.rgba"); img(K + 2).tofile(f"{O}/truth2.rgba")
print("ok", O)

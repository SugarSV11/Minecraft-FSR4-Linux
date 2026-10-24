#!/usr/bin/env python3
"""vq-compare.py DIR NAME... : for each sweep case (static/slow/fast) compare vq_<mode>_<case>.png against vq_native_<case>.png:
PSNR, mean abs diff, best integer shift in [-2,2]^2 (a non-zero best shift = systematic misalignment = jitter / motion-vector error), and sharpness (gradient energy ratio vs native)."""
import sys, numpy as np
from PIL import Image
d = sys.argv[1]
def load(n): return np.asarray(Image.open(f"{d}/{n}.png").convert('L'), dtype=np.float64)
def psnr(a, b): m = ((a - b) ** 2).mean(); return 99 if m == 0 else 10 * np.log10(255 ** 2 / m)
def grad(a): return np.abs(np.diff(a, axis=0)).mean() + np.abs(np.diff(a, axis=1)).mean()
for case in ('static', 'slow', 'fast', 'move'):
    nat = load(f"vq_native_{case}"); c = (slice(80, 700), slice(80, 1500))   # world only: excludes the chat (fades at different times per run), hotbar and the hand
    print(f"== {case}")
    for mode in sys.argv[2:]:
        im = load(f"vq_{mode}_{case}"); best = (-1, 0, 0)
        for dy in range(-2, 3):
            for dx in range(-2, 3):
                p = psnr(np.roll(im, (dy, dx), (0, 1))[c], nat[c])
                if p > best[0]: best = (p, dy, dx)
        print(f"  {mode:5s} PSNR vs native {psnr(im[c], nat[c]):6.2f} dB | best shift (dy,dx)=({best[1]},{best[2]}) -> {best[0]:.2f} dB | sharpness vs native {grad(im[c]) / grad(nat[c]):.3f}")

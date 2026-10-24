#!/usr/bin/env python3
"""hud-diff.py PREFIX...: for PREFIX_real.png / PREFIX_gen.png (1920x1080) report the mean abs diff in HUD regions (hotbar, toast, crosshair) and the whole frame."""
import sys, numpy as np
from PIL import Image
R = {'hotbar': (1000, 1080, 590, 1330), 'toast': (0, 125, 1280, 1920), 'crosshair': (520, 560, 940, 980), 'world(center)': (300, 700, 400, 900)}
for p in sys.argv[1:]:
    a = np.asarray(Image.open(p + '_real.png').convert('RGB'), dtype=float); b = np.asarray(Image.open(p + '_gen.png').convert('RGB'), dtype=float); d = np.abs(a - b).mean(axis=2)
    print(p, ' '.join(f"{k}={d[y0:y1, x0:x1].mean():.2f}" for k, (y0, y1, x0, x1) in R.items()), f"whole={d.mean():.2f}")

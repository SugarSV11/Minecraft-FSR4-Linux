#!/usr/bin/env python3
"""psnr.py A.png B.png [...]: PSNR / mean abs diff between the first image and each following one (same size)."""
import sys, numpy as np
from PIL import Image
a = np.asarray(Image.open(sys.argv[1]).convert('RGB'), dtype=np.float64)
for f in sys.argv[2:]:
    b = np.asarray(Image.open(f).convert('RGB'), dtype=np.float64)
    if a.shape != b.shape: print(f, 'size mismatch', a.shape, b.shape); continue
    mse = ((a - b) ** 2).mean(); mad = np.abs(a - b).mean()
    print(f"{f}: PSNR={'inf' if mse == 0 else round(10*np.log10(255**2/mse), 2)} dB  meanAbsDiff={mad:.3f}  maxDiff={np.abs(a-b).max():.0f}")

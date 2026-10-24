"""Ghosting / duplication / blur / flicker metrics for generated (interpolated) frames (docs/NEURAL_GHOST_METRICS.md).

PSNR rewards blur and "average of two images": a double image can score the same PSNR as a clean but slightly displaced one. These metrics diagnose WHAT is wrong, per frame, against the TRUE intermediate frame:

  excess_f / excess_c   spurious edge energy: gradient energy of the output that has NO counterpart (within a tolerance of 2 / 6 px) in the truth, divided by the truth's edge energy.
                        A double edge or a duplicated smoke puff adds edges that the truth does not have. Fine scale (sigma 1) and coarse scale (sigma 3, low-contrast smoke).
  missing_f / missing_c missing edge energy: truth edges that the output lacks (blur, lost detail, deleted objects), same normalisation.
  hf_ratio / hf_corr    fine-detail check: high-pass energy of the output relative to the truth, and the correlation of the two high-pass images (blur lowers both, noise lowers only the correlation).
  dup%                  area share where the error is explained by BOTH real frames at once: per 32x32 tile, least squares of (out - truth) on (C0 - truth, C1 - truth) over pixels where the real frames differ from the
                        truth; a tile counts as a double image when both weights >= 0.2 and the fit explains >= 50 % of the residual energy (the temporal blend is the canonical example).
  stale%                same fit, one weight >= 0.7 and the other < 0.2: a stale copy of one real frame.
  tdiff                 temporal consistency of the sequence [C0, g1..gn, C1]: mean |d(out) - d(truth)| of consecutive frames, relative to the truth's own temporal change (flicker / swim).
  flick                 mean |temporal change| of the output relative to the truth's (the older flicker number; >1 = jumpier than the truth).
  ms_ssim / gsim        3-scale SSIM (luminance) and gradient-magnitude similarity: structural, not purely pixel-wise.
  hole_mae / hole_gross error inside the generator's hole-filled / disoccluded pixels (mean abs error, share >= 16 levels).
Regions: all, moving (|C1 - C0| >= 12 levels, dilated: where anything changes between the real frames) and hole. No learned perceptual metric (no weights available offline); everything is deterministic.
Definitions are frozen BEFORE they were applied to any corrector model; the negative controls (ghostcontrols.py) only check that each metric reacts to what it claims to detect."""
import numpy as np
from scipy import ndimage as ndi

def lum(rgb):
    return 0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2]

def _grad(l, sigma):
    s = ndi.gaussian_filter(l, sigma) if sigma > 0 else l
    return np.hypot(ndi.sobel(s, 0), ndi.sobel(s, 1)) / 8.0

def edge_terms(lo, lt, sigma, tol):
    eo, et = _grad(lo, sigma), _grad(lt, sigma); k = 2 * tol + 1
    eps = 1e-6; den = et.sum() + eps
    return float(np.maximum(0, eo - ndi.maximum_filter(et, k)).sum() / den), float(np.maximum(0, et - ndi.maximum_filter(eo, k)).sum() / den)

def hf(l, sigma=2.0): return l - ndi.gaussian_filter(l, sigma)

def box(x, r=3):
    return ndi.uniform_filter(x, 2 * r + 1, mode="nearest")

def ssim_map(x, y, r=3):
    c1, c2 = (0.01 * 255) ** 2, (0.03 * 255) ** 2; ma, mb = box(x, r), box(y, r); va = box(x * x, r) - ma * ma; vb = box(y * y, r) - mb * mb; cab = box(x * y, r) - ma * mb
    return ((2 * ma * mb + c1) * (2 * cab + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2))

def ms_ssim(x, y):
    v = []
    for _ in range(3):
        v.append(float(ssim_map(x, y).mean())); h, w = x.shape[0] // 2 * 2, x.shape[1] // 2 * 2
        x = x[:h, :w].reshape(h // 2, 2, w // 2, 2).mean((1, 3)); y = y[:h, :w].reshape(h // 2, 2, w // 2, 2).mean((1, 3))
    return float(np.prod(v) ** (1 / 3))

def gsim(lo, lt):
    go, gt = _grad(lo, 1.0), _grad(lt, 1.0); c = 4.0
    return float(((2 * go * gt + c) / (go * go + gt * gt + c)).mean())

_SH = [(int(round(r * np.cos(a))), int(round(r * np.sin(a)))) for r in (3, 5, 8, 12, 16) for a in np.arange(8) * np.pi / 4]   # shifts in half-resolution pixels (6..32 px at full resolution)

def dupcorr(lo, lt, region=None):
    """Duplication score: largest POSITIVE correlation between the residual (out - truth) and a displaced copy of the truth's fine structure. A ghost / duplicated puff / double edge is, by
    construction, the truth's own content added again at an offset; noise and blur (negative correlation at small lags) do not produce it."""
    h, w = lo.shape[0] // 2 * 2, lo.shape[1] // 2 * 2
    d = lambda x: x[:h, :w].reshape(h // 2, 2, w // 2, 2).mean((1, 3))
    o, t = d(lo), d(lt); r = o - t; ht = t - ndi.gaussian_filter(t, 1.5)
    if region is not None:
        m = region[:h:2, :w:2]
        if m.sum() < 200: return np.nan
        r = r * m; ht = ht * m
    nr = np.sqrt((r * r).sum()) + 1e-9; best = 0.0
    for dx, dy in _SH:
        s = np.roll(np.roll(ht, dx, 1), dy, 0); best = max(best, float((r * s).sum() / (nr * (np.sqrt((s * s).sum()) + 1e-9))))
    return best

def mix_terms(lo, lt, l0, l1):
    """Pixel tests on the real frames: mix% = output is a temporal MIX of the real frames (within 4 levels of w*C0+(1-w)*C1 for some w in {.2,.35,.5,.65,.8}) where the real frames differ by >= 24 and the truth is NOT that mix;
    stale% = output equals ONE real frame there while the truth does not. Both as % of the image area."""
    sep = np.abs(l0 - l1) >= 24; mix = np.zeros_like(sep); 
    for w in (0.2, 0.35, 0.5, 0.65, 0.8):
        mx = w * l0 + (1 - w) * l1; mix |= (np.abs(lo - mx) < 4) & (np.abs(lt - mx) >= 16)
    st = ((np.abs(lo - l0) < 4) | (np.abs(lo - l1) < 4)) & (np.abs(lt - lo) >= 16)
    return 100.0 * float((sep & mix).mean()), 100.0 * float((sep & st).mean())

def frame_metrics(out, truth, c0, c1, hole=None):
    """out/truth/c0/c1: float arrays HxWx3 in 0..255. hole: optional bool HxW (generator hole-filled pixels). Returns a flat dict."""
    lo, lt, l0, l1 = lum(out), lum(truth), lum(c0), lum(c1)
    mov = ndi.binary_dilation(np.abs(l1 - l0) >= 12, iterations=4)
    res = {}
    for sig, tol, nm in ((1.0, 2, "f"), (3.0, 6, "c")):
        ex, ms = edge_terms(lo, lt, sig, tol); res["excess_" + nm] = ex; res["missing_" + nm] = ms
    ho, ht = hf(lo), hf(lt); res["hf_ratio"] = float(np.abs(ho).sum() / max(1e-6, np.abs(ht).sum()))
    res["hf_corr"] = float((ho * ht).sum() / np.sqrt(max(1e-9, (ho * ho).sum() * (ht * ht).sum())))
    res["dupcorr"] = dupcorr(lo, lt); res["dupcorr_mov"] = dupcorr(lo, lt, mov); res["mix%"], res["stale%"] = mix_terms(lo, lt, l0, l1)
    res["ms_ssim"] = ms_ssim(lo, lt); res["gsim"] = gsim(lo, lt)
    err = np.abs(lo - lt); res["mae"] = float(err.mean()); res["mse"] = float(((lo - lt) ** 2).mean()); res["mae_mov"] = float(err[mov].mean()) if mov.sum() > 50 else np.nan; res["moving%"] = 100.0 * float(mov.mean())
    res["gross_mov%"] = 100.0 * float((err[mov] >= 16).mean()) if mov.sum() > 50 else np.nan
    if hole is not None and hole.sum() > 50: res["hole_mae"] = float(err[hole].mean()); res["hole_gross%"] = 100.0 * float((err[hole] >= 16).mean())
    else: res["hole_mae"] = res["hole_gross%"] = np.nan
    return res

def sequence_metrics(outs, truths, c0, c1):
    """outs/truths: lists of luminance frames between the real frames c0 (lum) and c1 (lum)."""
    so = [lum(c0)] + [lum(o) for o in outs] + [lum(c1)]; st = [lum(c0)] + [lum(t) for t in truths] + [lum(c1)]
    do = [so[i + 1] - so[i] for i in range(len(so) - 1)]; dt = [st[i + 1] - st[i] for i in range(len(st) - 1)]
    mt = np.mean([np.abs(d).mean() for d in dt]); mo = np.mean([np.abs(d).mean() for d in do])
    return {"tdiff": float(np.mean([np.abs(a - b).mean() for a, b in zip(do, dt)]) / max(1e-6, mt)), "flick": float(mo / max(1e-6, mt))}

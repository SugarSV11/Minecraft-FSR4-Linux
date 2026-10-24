#!/usr/bin/env python3
"""Deterministic particle test for the REAL FG generator (build/cmake/fgx3_replay = native/src/fgx3_pass.cpp + shaders/fgx3.comp).

Scene (1280x720, one convention for colour, depth and motion planes - fgx3_replay applies no flip): procedural textured background + translucent soft smoke sprites
(alpha profile and internal texture are attached to the sprite, i.e. the texture moves with the particle). F0 = t 0, F1 = t 1, truth frames at t 0.25/0.5/0.75 (x4).
The motion/depth planes are built in several VARIANTS that reproduce what the prep pass writes:
  bg        camera motion only (FSR4_PARTICLE_MV=0)
  disc      shaders/fsr4_prep.comp mode 2 as shipped: inscribed disc (dd.dd <= 0.5 of the half extent), particle depth + particle motion (FSR4_PARTICLE_MV=1)
  disc_mv   same disc, particle motion, but NO depth write (isolates the depth effect)
  quad      the whole quad instead of the disc
  oracle_a  per-pixel "alpha-dominant" assignment: particle motion only where the sprite alpha >= 0.5, background motion elsewhere
usage: synth_particles.py OUTDIR [--replay build/cmake/fgx3_replay]  -> prints one line per scenario x variant
"""
import sys, os, subprocess, json
import numpy as np
from scipy import ndimage as ndi
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "ai", "neural"))
import ghostmetrics as G

W, H = 1280, 720
TS = (0.25, 0.5, 0.75)


def noise_tex(seed, n, sig):
    r = np.random.default_rng(seed)
    a = ndi.gaussian_filter(r.random((n, n, 3)), (sig, sig, 0), mode="wrap")
    a = (a - a.min()) / (a.max() - a.min())
    return a


def make_scene(rng_seed, n_part, a0, dc, dp_extra, hf_range=(40, 80)):
    r = np.random.default_rng(rng_seed)
    bg = np.zeros((H, W, 3), np.float32)
    big = noise_tex(rng_seed, 1024, 1.2) * 0.6 + noise_tex(rng_seed + 1, 1024, 5) * 0.4
    big = 55 + 150 * big
    parts = []
    for i in range(n_part):
        hf = r.uniform(*hf_range)
        c0 = np.array([r.uniform(250 + hf, W - 250 - hf), r.uniform(150 + hf, H - 150 - hf)])
        # particle screen displacement per interval = camera motion + its own motion
        own = r.normal(0, 1, 2); own = own / (np.linalg.norm(own) + 1e-9) * dp_extra
        parts.append(dict(hf=hf, c0=c0, dp=np.array(dc) + own, tex=noise_tex(rng_seed + 10 + i, 128, 2.0), grey=r.uniform(120, 190)))
    return big, parts


def sample_wrap(tex, ys, xs):
    return np.stack([ndi.map_coordinates(tex[..., c], [ys % tex.shape[0], xs % tex.shape[1]], order=1, mode="grid-wrap") for c in range(3)], -1)


def sprite_alpha(p, t, a0):
    """alpha (HxW), sprite colour (HxWx3) of a particle at time t."""
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    c = p["c0"] + t * p["dp"]
    u = (xx + 0.5 - c[0]) / p["hf"]; v = (yy + 0.5 - c[1]) / p["hf"]
    inq = (np.abs(u) <= 1) & (np.abs(v) <= 1)
    prof = np.exp(-3.0 * (u * u + v * v))
    tex = np.stack([ndi.map_coordinates(p["tex"][..., k], [(v * 0.5 + 0.5) * 127, (u * 0.5 + 0.5) * 127], order=1, mode="nearest") for k in range(3)], -1)
    a = np.where(inq, a0 * prof * (0.6 + 0.8 * tex[..., 0]), 0.0).clip(0, 1)
    col = p["grey"] * (0.75 + 0.5 * tex)
    return a.astype(np.float32), col.astype(np.float32), inq


def render(big, parts, t, dc, a0):
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    img = sample_wrap(big[..., None].repeat(1, -1) if big.ndim == 2 else big, yy + 0.5 - t * dc[1] + 100, xx + 0.5 - t * dc[0] + 100)
    amax = np.zeros((H, W), np.float32)
    for p in parts:
        a, col, _ = sprite_alpha(p, t, a0)
        img = img * (1 - a[..., None]) + col * a[..., None]
        amax = np.maximum(amax, a)
    return img.clip(0, 255), amax


def planes(parts, t_frame, variant, dc, a0):
    """depth (reversed-Z) and motion (mv = prev - cur, px) at frame time t_frame (0 or 1)."""
    depth = np.full((H, W), 0.02, np.float32)
    mv = np.zeros((H, W, 2), np.float32); mv[..., 0] = -dc[0]; mv[..., 1] = -dc[1]
    if variant == "bg":
        return depth, mv
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    for p in parts:
        c = p["c0"] + t_frame * p["dp"]; hf = p["hf"]
        dd = np.stack([(xx + 0.5 - c[0]) / hf, (yy + 0.5 - c[1]) / hf], -1)
        if variant in ("disc", "disc_mv"):
            m = ((dd ** 2).sum(-1) <= 0.5) & (np.abs(dd) <= 1).all(-1)
        elif variant == "quad":
            m = (np.abs(dd) <= 1).all(-1)
        elif variant == "oracle_a":
            a, _, _ = sprite_alpha(p, t_frame, a0)
            m = a >= 0.5
        else:
            raise ValueError(variant)
        if variant != "disc_mv": depth[m] = 0.05
        mv[m] = -p["dp"]
    return depth, mv


def run_case(cdir, big, parts, dc, a0, variant, replay):
    os.makedirs(cdir, exist_ok=True)
    c0, _ = render(big, parts, 0, dc, a0); c1, _ = render(big, parts, 1, dc, a0)
    d0, _ = planes(parts, 0, variant, dc, a0); d1, mv = planes(parts, 1, variant, dc, a0)
    rgba = lambda x: np.concatenate([x.round().astype(np.uint8), np.full((H, W, 1), 255, np.uint8)], -1).tofile
    rgba(c0)(f"{cdir}/c0.rgba"); rgba(c1)(f"{cdir}/c1.rgba")
    d0.tofile(f"{cdir}/d0.f32"); d1.tofile(f"{cdir}/d1.f32"); mv.astype(np.float32).tofile(f"{cdir}/mv1.f32")
    open(f"{cdir}/t.txt", "w").write(" ".join(f"{t:.5f}" for t in TS) + "\n")
    subprocess.run([replay, str(W), str(H), cdir], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    outs = [np.fromfile(f"{cdir}/out_{i+1}.rgba", np.uint8).reshape(H, W, 4)[..., :3].astype(np.float32) for i in range(len(TS))]
    import shutil; shutil.rmtree(cdir, ignore_errors=True)   # ~30 MB per case: keep the disk (and tmpfs) free
    return c0, c1, outs


def metrics(c0, c1, outs, truths, pmask):
    rows = []
    for o, t in zip(outs, truths):
        m = G.frame_metrics(o, t, c0, c1)
        lo, lt = G.lum(o), G.lum(t); err = np.abs(lo - lt)
        m["psnr"] = float(10 * np.log10(255 ** 2 / max(1e-9, ((o - t) ** 2).mean())))
        m["p_mae"] = float(err[pmask].mean()); m["p_gross%"] = 100 * float((err[pmask] >= 16).mean())
        m["p_dupcorr"] = G.dupcorr(lo, lt, pmask)
        m["bg_mae"] = float(err[~pmask].mean())
        rows.append(m)
    keys = rows[0].keys()
    return {k: float(np.nanmean([r[k] for r in rows])) for k in keys}, [float(r["psnr"]) for r in rows]


SCENARIOS = [
    # name, n particles, alpha, camera motion (px per interval), particle own motion (px per interval)
    ("iso_static_slow", 1, 0.6, (0, 0), 3.0),
    ("iso_static_fast", 1, 0.6, (0, 0), 10.0),
    ("iso_pan_slow", 1, 0.6, (12, 4), 3.0),
    ("iso_pan_fast", 1, 0.6, (12, 4), 10.0),
    ("iso_static_opaque", 1, 0.95, (0, 0), 6.0),
    ("iso_static_faint", 1, 0.3, (0, 0), 6.0),
    ("multi_static", 8, 0.5, (0, 0), 5.0),
    ("multi_pan", 8, 0.5, (12, 4), 5.0),
]
VARIANTS = ["bg", "disc", "disc_mv", "quad", "oracle_a"]


def main():
    out = sys.argv[1]; replay = "build/cmake/fgx3_replay"
    if "--replay" in sys.argv: replay = sys.argv[sys.argv.index("--replay") + 1]
    only = [a for a in sys.argv[2:] if a in dict((s[0], 1) for s in SCENARIOS)]
    res = {}
    for si, (name, n, a0, dc, dpx) in enumerate(SCENARIOS):
        if only and name not in only: continue
        big, parts = make_scene(100 + si, n, a0, dc, dpx)
        truths = [render(big, parts, t, dc, a0) for t in TS]
        pmask = np.zeros((H, W), bool)
        for p in parts:
            for t in (0, *TS, 1):
                a, _, _ = sprite_alpha(p, t, a0); pmask |= a > 0.04
        tr = [x[0] for x in truths]
        for v in VARIANTS:
            c0, c1, outs = run_case(f"{out}/{name}_{v}", big, parts, dc, a0, v, replay)
            m, ps = metrics(c0, c1, outs, tr, pmask); res[f"{name}/{v}"] = m
            # blend baseline (no motion) once per scenario
            print(f"{name:20s} {v:9s} psnr {m['psnr']:6.2f} | particle MAE {m['p_mae']:6.2f} gross% {m['p_gross%']:5.1f} dupcorr {m['p_dupcorr']:.3f} | bg MAE {m['bg_mae']:5.2f} | excess_c {m['excess_c']:.3f} dup {m['dupcorr']:.3f}", flush=True)
        # temporal-mix baseline: what a plain 50/50 blend gives (reference for 'worse than blending')
        bl = [(c0 * (1 - t) + c1 * t) for t in TS]
        m, _ = metrics(c0, c1, bl, tr, pmask)
        print(f"{name:20s} {'blend':9s} psnr {m['psnr']:6.2f} | particle MAE {m['p_mae']:6.2f} gross% {m['p_gross%']:5.1f} dupcorr {m['p_dupcorr']:.3f} | bg MAE {m['bg_mae']:5.2f}", flush=True)
        res[f"{name}/blend"] = m
    json.dump(res, open(f"{out}/results.json", "w"), indent=1)


if __name__ == "__main__":
    main()

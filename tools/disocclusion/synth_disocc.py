#!/usr/bin/env python3
"""Deterministic disocclusion test for the REAL FG generator (build/cmake/fgx3_replay).
Opaque textured rectangles (depth nearer) slide over a textured background; the planes (depth reversed-Z, motion mv = prev - cur, px) are exact. Truth frames at t = .25/.5/.75 (x4) are analytic.
Regions scored separately: 'reveal' = pixels that show background at time t but were covered by an object in F0 (information only in F1); 'cover' = pixels covered by an object at t that show background in F0;
'rest' = everything else. Also the blend baseline.   usage: synth_disocc.py OUTDIR [--replay path] [--keep]   env FSR4_X3_VARIANT passes through to the generator."""
import sys, os, subprocess, shutil, json
import numpy as np
from scipy import ndimage as ndi
W, H = 1280, 720; TS = (0.25, 0.5, 0.75)
def tex(seed, n, sig, lo, hi):
    r = np.random.default_rng(seed); a = ndi.gaussian_filter(r.random((n, n, 3)), (sig, sig, 0), mode="wrap"); a = (a - a.min()) / (a.max() - a.min()); return lo + (hi - lo) * a
def samp(t, ys, xs): return np.stack([ndi.map_coordinates(t[..., c], [ys % t.shape[0], xs % t.shape[1]], order=1, mode="grid-wrap") for c in range(3)], -1)
def scene(seed, nobj, dobj, dcam):
    r = np.random.default_rng(seed); bg = tex(seed, 1024, 1.5, 40, 200); objs = []
    for i in range(nobj):
        w, h = r.uniform(120, 260), r.uniform(120, 260); c = np.array([r.uniform(300, W - 300), r.uniform(200, H - 200)])
        d = r.normal(0, 1, 2); d = d / np.linalg.norm(d) * dobj + np.array(dcam)
        objs.append(dict(c=c, w=w, h=h, d=d, tex=tex(seed + 5 + i, 256, 1.0, 30, 230)))
    return bg, objs
def render(bg, objs, t, dcam):
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32); img = samp(bg, yy + .5 - t * dcam[1] + 50, xx + .5 - t * dcam[0] + 50); cover = np.zeros((H, W), bool)
    for o in objs:
        c = o["c"] + t * o["d"]; u = (xx + .5 - c[0]); v = (yy + .5 - c[1]); m = (np.abs(u) <= o["w"] / 2) & (np.abs(v) <= o["h"] / 2)
        s = samp(o["tex"], v - 0 + 128 + (o["h"] / 2) * 0, u + 128)   # texture is attached to the object
        img[m] = s[m]; cover |= m
    return img.clip(0, 255), cover
def planes(objs, t, dcam):
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32); depth = np.full((H, W), 0.02, np.float32); mv = np.zeros((H, W, 2), np.float32); mv[..., 0] = -dcam[0]; mv[..., 1] = -dcam[1]
    for o in objs:
        c = o["c"] + t * o["d"]; m = (np.abs(xx + .5 - c[0]) <= o["w"] / 2) & (np.abs(yy + .5 - c[1]) <= o["h"] / 2); depth[m] = 0.1; mv[m] = -o["d"]
    return depth, mv
SC = [("slow_static", 2, 12, (0, 0)), ("fast_static", 2, 40, (0, 0)), ("slow_pan", 2, 12, (10, 3)), ("fast_pan", 3, 40, (10, 3)), ("huge", 2, 90, (0, 0))]
def main():
    out = sys.argv[1]; replay = sys.argv[sys.argv.index("--replay") + 1] if "--replay" in sys.argv else "build/cmake/fgx3_replay"; res = {}
    for si, (name, n, dobj, dcam) in enumerate(SC):
        bg, objs = scene(200 + si, n, dobj, dcam); tr = [render(bg, objs, t, dcam) for t in TS]; c0, cov0 = render(bg, objs, 0, dcam); c1, cov1 = render(bg, objs, 1, dcam)
        d0, _ = planes(objs, 0, dcam); d1, mv = planes(objs, 1, dcam); cd = f"{out}/{name}"; os.makedirs(cd, exist_ok=True)
        rg = lambda x, p: np.concatenate([x.round().astype(np.uint8), np.full((H, W, 1), 255, np.uint8)], -1).tofile(p)
        rg(c0, f"{cd}/c0.rgba"); rg(c1, f"{cd}/c1.rgba"); d0.tofile(f"{cd}/d0.f32"); d1.tofile(f"{cd}/d1.f32"); mv.tofile(f"{cd}/mv1.f32"); open(f"{cd}/t.txt", "w").write(" ".join(f"{t:.5f}" for t in TS) + "\n")
        if "--keep" in sys.argv:
            for i, (truth, _) in enumerate(tr, 1): rg(truth, f"{cd}/truth{i}.rgba")
        subprocess.run([replay, str(W), str(H), cd], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        outs = [np.fromfile(f"{cd}/out_{i+1}.rgba", np.uint8).reshape(H, W, 4)[..., :3].astype(np.float32) for i in range(3)]
        if "--keep" not in sys.argv: shutil.rmtree(cd)
        row = {}
        for tag, frames in (("gen", outs), ("blend", [c0 * (1 - t) + c1 * t for t in TS])):
            acc = {"reveal": [], "cover": [], "rest": [], "all": []}
            for (tf, covt), o in zip(tr, frames):
                e = np.abs(o - tf).mean(-1); reveal = (~covt) & cov0; cover = covt & (~cov0)
                for k, m in (("reveal", reveal), ("cover", cover), ("rest", ~reveal & ~cover), ("all", np.ones_like(reveal))):
                    acc[k].append((e[m].mean(), 100 * (e[m] >= 16).mean(), m.sum()))
            row[tag] = {k: (float(np.mean([a[0] for a in v])), float(np.mean([a[1] for a in v])), int(np.mean([a[2] for a in v]))) for k, v in acc.items()}
        res[name] = row
        f = lambda t, k: f"{row[t][k][0]:6.2f}/{row[t][k][1]:5.1f}%"
        print(f"{name:12s} reveal({row['gen']['reveal'][2]:6d}px) gen {f('gen','reveal')} blend {f('blend','reveal')} | cover gen {f('gen','cover')} blend {f('blend','cover')} | rest gen {f('gen','rest')} blend {f('blend','rest')}", flush=True)
    json.dump(res, open(f"{out}/results.json", "w"), indent=1)
if __name__ == "__main__": main()

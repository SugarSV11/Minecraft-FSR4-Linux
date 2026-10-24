"""Neural-corrector dataset (docs/NEURAL_DATASET.md).
Per generated frame (case, i) the sample is 22 uint8 channels (all images are 1280x720, bottom-up like the game's targets, RGB):
  0-2   G   generated frame (what the shader produces today)
  3-5   A   F0 sample at the generator's warped position (feat5)
  6-8   B   F1 sample at the generator's warped position (feat6)
  9-11  C0  previous real frame
  12-14 C1  next real frame
  15-17 F   features: has-source / depth-consistent / hole-filled (feat7 RGB)
  18-20 M   features: mv x, mv y (0.5 = 0, 1/64 px per level), relative depth change (feat8 RGB)
  21    T   interpolation time t * 255
Target Y = true intermediate frame (3 channels). Scenes are split WHOLE (no sequence is shared between TRAIN / VAL / TEST).
"""
import sys as _sys, os as _os; _sys.path.insert(0, _os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "..")); import ncpaths as NC
import os, json, numpy as np
W, H, C = 1280, 720, 22
ROOT = NC.evidence(11)   # session 12 scenes live in FSR4-evidence-session12 (see build())
SPLIT_V1 = {"train": ["baseline", "vegetation", "water", "animals", "night", "monsters"], "val": ["mixed"], "test": ["disocclusion", "transparency", "particles", "fastentity"]}   # session 11 (kept to reproduce docs/NEURAL_EVALUATION.md)
# Session 12, frozen BEFORE any new result: the four old TEST scenes were looked at in session 11, so they become VALIDATION; the TEST set is six scenes recorded afterwards (scripts/x3-scene.sh occl_c fast_c part_c transp_c turn_b combo_b)
# and is only evaluated once per candidate model family at the very end.
SPLIT_V2 = {"train": ["baseline", "vegetation", "water", "animals", "night", "monsters", "occl_b", "fast_b", "part_b", "transp_b", "water_b", "animals_b", "turn_a", "combo_a"],
            "val": ["mixed", "disocclusion", "transparency", "particles", "fastentity"], "test": ["occl_c", "fast_c", "part_c", "transp_c", "turn_b", "combo_b"]}   # first session-12 split; its TEST was evaluated once (R and b0g) and is CONSUMED
# Second split (current): the consumed TEST becomes validation; the new TEST (6 scenes recorded afterwards: occl_d fast_d part_d transp_d turn_c combo_c) is sealed.
SPLIT = {"train": SPLIT_V2["train"], "val": SPLIT_V2["val"] + SPLIT_V2["test"], "test": ["occl_d", "fast_d", "part_d", "transp_d", "turn_c", "combo_c"]}
def rd(p): return np.fromfile(p, dtype=np.uint8).reshape(H, W, 4)[..., :3]
def build(scene, out_dir, step_a=6, step_b=2, ksplit=60, rec=None, cdir="cases"):
    rec = rec or scene
    root = next((r for r in (NC.evidence(n) for n in (13, 12)) if os.path.isdir(f"{r}/{rec}")), ROOT)
    cases = [l.strip() for l in open(f"{root}/{rec}/{cdir}/cases.txt") if l.strip()]
    sel = []; ia = ib = 0
    for c in cases:
        k = int(os.path.basename(c)[1:])
        if k < ksplit - 4:
            if ia % step_a == 0: sel.append((c, "A"))
            ia += 1
        elif k >= ksplit:
            if ib % step_b == 0: sel.append((c, "B"))
            ib += 1
    items = []
    for c, ph in sel:
        t = [float(x) for x in open(f"{c}/t.txt").read().split()]
        for i in range(1, len(t) + 1): items.append((c, ph, i, t[i - 1]))
    os.makedirs(out_dir, exist_ok=True); N = len(items)
    X = np.lib.format.open_memmap(f"{out_dir}/{scene}_X.npy", mode="w+", dtype=np.uint8, shape=(N, C, H, W))
    Y = np.lib.format.open_memmap(f"{out_dir}/{scene}_Y.npy", mode="w+", dtype=np.uint8, shape=(N, 3, H, W))
    for n, (c, ph, i, t) in enumerate(items):
        ch = [rd(f"{c}/out_{i}.rgba"), rd(f"{c}/feat5_{i}.rgba"), rd(f"{c}/feat6_{i}.rgba"), rd(f"{c}/c0.rgba"), rd(f"{c}/c1.rgba"), rd(f"{c}/feat7_{i}.rgba"), rd(f"{c}/feat8_{i}.rgba")]
        X[n, :21] = np.concatenate(ch, -1).transpose(2, 0, 1); X[n, 21] = int(round(t * 255)); Y[n] = rd(f"{c}/truth{i}.rgba").transpose(2, 0, 1)
    X.flush(); Y.flush()
    json.dump([{"case": c, "phase": ph, "i": i, "t": t} for c, ph, i, t in items], open(f"{out_dir}/{scene}_meta.json", "w"), indent=0)
    return N
def load(scene, d):
    return np.load(f"{d}/{scene}_X.npy", mmap_mode="r"), np.load(f"{d}/{scene}_Y.npy", mmap_mode="r"), json.load(open(f"{d}/{scene}_meta.json"))
if __name__ == "__main__":
    import sys
    out = sys.argv[1]
    for sc in sys.argv[2:]: print(sc, build(sc, out), "frames")
# Session 13 (frozen before any session-13 model): TEST2 was evaluated and analysed in session 12 -> it joins the validation scenes (no scene may be reused as a sealed test).
# TEST3 = six scenes recorded afterwards with FSR4_PARTICLE_AB=1 (variants _A: generator without particle vectors, _B: with); sealed, evaluated once per final family. The v3_* validation scenes are the A variants.
SPLIT3 = {"train": SPLIT["train"], "val": SPLIT["val"] + SPLIT["test"] + ["v3_part1_A", "v3_part2_A", "v3_fast_A", "v3_turn_A"], "test": ["t3_part1_A", "t3_part2_A", "t3_transp_A", "t3_occl_A", "t3_fast_A", "t3_combo_A"], "test_b": ["t3_part1_B", "t3_part2_B", "t3_transp_B", "t3_occl_B", "t3_fast_B", "t3_combo_B"]}

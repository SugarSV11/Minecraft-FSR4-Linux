#!/usr/bin/env python3
"""suite-report.py TAG:LABEL ... : table of the per-mode benchmark sessions benchmarks/s10/suite_TAG.{testlog,gpu}: mean (and per-window values) of rendered/generated/presented FPS, GPU busy %, VRAM, CPU cores used by the game process
(from /proc jiffies sampled every 0.2 s), p99 frame time, hook CPU, FG GPU time. Windows are the harness 'bench rN' windows (200 ticks each)."""
import sys, re, statistics as st, os
HZ = os.sysconf("SC_CLK_TCK")
print("| modo | renderizados FPS | generados FPS | **presentados FPS** | GPU busy % | VRAM MB | CPU núcleos (juego) | p99 ms | 1% low FPS | FG GPU ms | lat. hook CPU ms |"); print("|---|---|---|---|---|---|---|---|---|---|---|")
for spec in sys.argv[1:]:
    tag, label = spec.split(":", 1); base = f"benchmarks/s10/suite_{tag}"
    log = open(base + ".testlog").read().splitlines(); gpu = [list(map(float, l.split())) for l in open(base + ".gpu") if len(l.split()) == 4]
    starts = {m.group(2): int(m.group(1)) for l in log if (m := re.match(r"(\d+) step \d+: bench (\S+) (\d+)", l))}
    rows = []
    for l in log:
        m = re.match(r"(\d+) BENCH ([^,\s]+),", l)
        if not m: continue
        d = dict(kv.split("=", 1) for kv in l.split(",")[4:] if "=" in kv); name = m.group(2); w = [g for g in gpu if name in starts and starts[name] <= g[0] <= int(m.group(1))]
        d["gpu"] = st.mean(g[1] for g in w) if w else float("nan"); d["vram"] = max(g[2] for g in w) if w else float("nan")
        d["cpu"] = ((w[-1][3] - w[0][3]) / HZ / ((w[-1][0] - w[0][0]) / 1000)) if len(w) > 1 else float("nan"); rows.append(d)
    def f(k, fmt="{:.1f}"):
        v = [float(r[k]) for r in rows if k in r and r[k] not in ("-1.000",)]
        if not v: return "-"
        return fmt.format(st.mean(v)) + " (" + "/".join(fmt.format(x) for x in v) + ")" if len(v) > 1 and k in ("rendered_fps", "presented_fps") else fmt.format(st.mean(v))
    print(f"| {label} | {f('rendered_fps')} | {f('generated_fps')} | **{f('presented_fps')}** | {f('gpu','{:.0f}')} | {f('vram','{:.0f}')} | {f('cpu','{:.2f}')} | {f('p99_ms','{:.2f}')} | {f('fps_1pct_low','{:.0f}')} | {f('fg_gpu_ms','{:.2f}')} | {f('fg_hook_cpu_ms','{:.2f}')} |")

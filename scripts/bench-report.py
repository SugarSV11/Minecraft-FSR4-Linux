#!/usr/bin/env python3
"""bench-report.py RUN.csv : merges bench.csv with the GPU/VRAM sampler (RUN.gpu) and the harness log (RUN.testlog) -> markdown table rows."""
import sys, re, statistics as st
base = sys.argv[1][:-4]
rows = [l.strip().split(',') for l in open(sys.argv[1])]
log = open(base + '.testlog').read().splitlines()
gpu = [tuple(map(float, l.split()))[:3] + ((float(l.split()[3]),) if len(l.split()) > 3 else (0.0,)) for l in open(base + '.gpu') if len(l.split()) in (3, 4)]
starts = {}
for l in log:
    m = re.match(r'(\d+) step \d+: bench (\S+) (\d+)', l)
    if m: starts[m.group(2)] = int(m.group(1))
ends = {}
for l in log:
    m = re.match(r'(\d+) BENCH ([^,\s]+),', l)
    if m: ends[m.group(2)] = int(m.group(1))
print('| run | mode | internal | FPS avg | FPS 1% low | frame ms avg | p99 ms | GPU busy % | VRAM MB (max) | FSR dispatch ms | render-thread cost ms | executed |')
print('|---|---|---|---|---|---|---|---|---|---|---|---|')
for r in rows:
    d = dict(kv.split('=', 1) for kv in r[4:] if '=' in kv)
    n = r[0]; w = [g for g in gpu if n in starts and n in ends and starts[n] <= g[0] <= ends[n]]
    busy = f"{st.mean(g[1] for g in w):.0f}" if w else '-'; vram = f"{max(g[2] for g in w):.0f}" if w else '-'
    ex = d.get('executed', 'null'); ex = ex.split('[')[0].strip() if ex != 'null' else 'native'
    mode = (r[1] + ' ' + r[2]) if ex != 'native' else 'NATIVE'
    print(f"| {n} | {mode} | {r[3]} | {d['fps_avg']} | {d['fps_1pct_low']} | {d['avg_ms']} | {d['p99_ms']} | {busy} | {vram} | {d.get('gpu_dispatch_ms','-') if ex != 'native' else '-'} | {d.get('bridge_render_thread_ms','-') if ex != 'native' else '-'} | {ex} |")

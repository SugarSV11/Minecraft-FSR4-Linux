#!/usr/bin/env python3
"""fg-report.py RUN.csv: Frame Generation rows (rendered / generated / presented FPS are SEPARATE quantities) as a markdown table."""
import sys
print('| run | FG mode | internal | rendered FPS | generated FPS | presented FPS (vkQueuePresent) | added latency ms | FG CPU in hook ms | FG IPC ms | FG GPU ms | present interval mean / p99 / max / std ms |')
print('|---|---|---|---|---|---|---|---|---|---|---|')
for l in open(sys.argv[1]):
    p = l.strip().split(','); d = dict(kv.split('=', 1) for kv in p[4:] if '=' in kv)
    print(f"| {p[0]} | {d.get('fg_mode','off')} | {p[3]} | {d['rendered_fps']} | {d['generated_fps']} | {d['presented_fps']} | {d['added_latency_ms']} | {d['fg_hook_cpu_ms']} | {d['fg_ipc_ms']} | {d['fg_gpu_ms']} | {d['present_iv_mean_ms']} / {d['present_iv_p99_ms']} / {d['present_iv_max_ms']} / {d['present_iv_std_ms']} |")

#!/usr/bin/env python3
"""probe-summary.py RUN.testlog...: mean over the BENCH windows of rendered/presented FPS and of the present_wait probe (observable frames, intervals, queue latency)."""
import sys, re, statistics as st
KEYS = ["rendered_fps", "generated_fps", "presented_fps", "obs_fps", "obs_iv_mean_ms", "obs_iv_p99_ms", "obs_queue_lat_mean_ms", "obs_queue_lat_p99_ms", "obs_same_flip_share", "obs_timeouts", "p99_ms"]
print("| run | " + " | ".join(KEYS) + " |"); print("|---|" + "---|" * len(KEYS))
for f in sys.argv[1:]:
    rows = [dict(kv.split("=", 1) for kv in l.split(",") if "=" in kv) for l in open(f) if " BENCH " in l]
    def m(k):
        v = [float(r[k]) for r in rows if k in r]
        return f"{st.mean(v):.2f}" if v else "-"
    print(f"| {f.split('/')[-1].replace('.testlog','')} (n={len(rows)}) | " + " | ".join(m(k) for k in KEYS) + " |")

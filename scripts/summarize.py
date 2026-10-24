#!/usr/bin/env python3
import sys
keys=['fps_avg','avg_ms','fps_1pct_low','p99_ms','gpu_dispatch_ms','submit_ms','gpuwait_ms','ipc_ms','helper_rec_ms','helper_wait_ms']
for f in sys.argv[1:]:
    print('##',f)
    for l in open(f):
        p=l.strip().split(',')
        d=dict(kv.split('=',1) for kv in p[4:] if '=' in kv)
        print(f"{p[0]:<22} {p[3]:<10} "+' '.join(f"{k}={d.get(k,'-')}" for k in keys))

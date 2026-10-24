#!/usr/bin/env bash
# bench-mode.sh OUT.csv MODE [reps] : 1080p benchmark of one mode in one game session (3+ consecutive 200-tick windows).
# MODE: native | fsr | fg2 | fg3 | fg4 | fsr_fg2 | fsr_fg3 | fsr_fg4   (extra env: FSR_QUALITY, BENCH_EXTRA_STEPS, EXTRA_ENV passed through)
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="$1"; MODE="$2"; REPS="${3:-3}"; Q="${FSR_QUALITY:-QUALITY}"
case "$MODE" in native) BE=NATIVE; EN=false; FG=false; M=2;; fsr) BE=FSR4_1_1; EN=true; FG=false; M=2;; fg2) BE=NATIVE; EN=false; FG=true; M=2;; fg3) BE=NATIVE; EN=false; FG=true; M=3;; fg4) BE=NATIVE; EN=false; FG=true; M=4;;
 fsr_fg2) BE=FSR4_1_1; EN=true; FG=true; M=2;; fsr_fg3) BE=FSR4_1_1; EN=true; FG=true; M=3;; fsr_fg4) BE=FSR4_1_1; EN=true; FG=true; M=4;; *) echo bad mode; exit 2;; esac
printf '{\n  "enabled": %s,\n  "backend": "%s",\n  "quality": "%s",\n  "sharpness": 0.5,\n  "debugOverlay": false,\n  "fpsCounter": false,\n  "frameGeneration": %s,\n  "fgPacing": false,\n  "fgMultiplier": %s\n}\n' "$EN" "$BE" "$Q" "$FG" "$M" > fabric/run/config/fsr4linux.json
STEPS="${PRE_STEPS:+$PRE_STEPS; }wait 100"; for i in $(seq 1 "$REPS"); do STEPS="$STEPS; bench r$i 200"; done
KEEP_CONFIG=1 WAIT_IDLE=1 scripts/bench-run.sh "$OUT" 1920 1080 "$BE $Q $EN" "$STEPS ${BENCH_EXTRA_STEPS:-}" >/dev/null 2>&1 || true
python3 - "$OUT" <<'PY'
import sys,re
b=sys.argv[1][:-4]; rows=[]
for l in open(b+'.testlog'):
    if ' BENCH ' in l:
        d=dict(kv.split('=',1) for kv in l.split(',') if '=' in kv); rows.append(d)
for k in ('rendered_fps','generated_fps','presented_fps','added_latency_ms','p99_ms','fps_1pct_low'):
    v=[float(r.get(k,0)) for r in rows]; print(f"{k}: {' / '.join(f'{x:.1f}' for x in v)}  mean {sum(v)/max(1,len(v)):.1f}")
PY

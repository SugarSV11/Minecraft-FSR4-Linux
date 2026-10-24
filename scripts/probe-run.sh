#!/usr/bin/env bash
# probe-run.sh MODE TAG [VAR=value ...]: benchmark with the present_wait probe (observable frames). The machine has a 100 Hz and a 60 Hz output and the game window lands on either;
# for VSYNC=true runs the run is repeated (up to 6 times) until the measured display interval is the 100 Hz one (10.0 +- 0.5 ms).
cd "$(dirname "$0")/.."; MODE="$1"; TAG="$2"; shift 2
for try in 1 2 3 4 5 6; do
  env "$@" FSR4_PRESENT_PROBE=1 timeout 500 scripts/bench-mode.sh benchmarks/s10/probe_$TAG.csv "$MODE" > /dev/null
  iv=$(grep -o "obs_iv_mean_ms=[0-9.]*" benchmarks/s10/probe_$TAG.testlog | head -1 | cut -d= -f2)
  case " $* " in *VSYNC=true*) python3 - "$iv" <<'PY' && break || echo "  (window on the 60 Hz output: interval $iv ms - repeating)" >&2
import sys; sys.exit(0 if abs(float(sys.argv[1] or 0) - 10.0) < 0.5 else 1)
PY
  ;; *) break;; esac
done
python3 scripts/probe-summary.py benchmarks/s10/probe_$TAG.testlog | tail -1

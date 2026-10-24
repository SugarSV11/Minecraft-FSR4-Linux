#!/usr/bin/env bash
# Session-10 round 2: int64 single-pass shader (default) vs 2-pass (FSR4_X3_NO_INT64=1) and particle-MV cost. Output benchmarks/s10/suite2_*.
cd "$(dirname "$0")/.."
for S in A B; do
 for spec in native:native:: fg3:fg3:: fg4:fg4:: fg4noi64:fg4:FSR4_X3_NO_INT64=1: fg4part:fg4:FSR4_PARTICLE_MV=1: fsr_fg3:fsr_fg3:: fsr_fg4:fsr_fg4::; do
  IFS=: read tag mode e1 e2 <<<"$spec"; echo "[$(date +%T)] $tag $S"
  env $e1 $e2 timeout 500 scripts/bench-mode.sh benchmarks/s10/suite2_${tag}_$S.csv $mode | grep -E "presented"
 done
done; echo "[$(date +%T)] suite2 done"

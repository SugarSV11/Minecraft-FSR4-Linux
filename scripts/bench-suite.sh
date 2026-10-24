#!/usr/bin/env bash
# Full 1080p benchmark suite (VSync OFF, render distance 16, fixed scene): every mode in two separate game sessions (A, B) of three 200-tick windows. Output: benchmarks/s10/suite_<mode>_<A|B>.*
# L = the previous one-submit-per-image presentation path (FSR4_BATCH_PRESENT=0), kept for the comparison.
cd "$(dirname "$0")/.."
for S in A B; do
 for spec in native:native fsr:fsr fg2:fg2 fg3:fg3 fg3L:fg3 fg4:fg4 fsr_fg2:fsr_fg2 fsr_fg3:fsr_fg3 fsr_fg3L:fsr_fg3 fsr_fg4:fsr_fg4; do
  tag=${spec%%:*}; mode=${spec##*:}; env=""; case $tag in *L) env="FSR4_BATCH_PRESENT=0";; esac
  echo "[$(date +%T)] $tag session $S"; env $env timeout 500 scripts/bench-mode.sh benchmarks/s10/suite_${tag}_$S.csv $mode | grep -E "presented"
 done
done
echo "[$(date +%T)] suite done"

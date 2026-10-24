#!/usr/bin/env bash
# neural-features.sh SCENE_DIR [STRIDE=4]: builds replay cases from a dense scene recording (scripts/x3-scene.sh), replays them through the REAL generator pass and stores, per generated frame i,
# the generator output (out_i.rgba) plus the shader's internal features: feat5_i = F0 sample (warped), feat6_i = F1 sample, feat7_i = (has-source, depth-consistent, hole-filled), feat8_i = (mv x, mv y, relative depth change).
set -euo pipefail; cd "$(dirname "$0")/.."; D="$1"; N="${2:-4}"; CD="${CASES_DIR:-cases}"
python3 scripts/x3_scene_dataset.py "$D" "$D/$CD" "$N" > /dev/null
CASES=$(cat "$D/$CD/cases.txt")
build/cmake/fgx3_replay 1280 720 $CASES > /dev/null 2>&1
for k in 5 6 7 8; do
  FSR4_X3_DEBUGVIEW=$k build/cmake/fgx3_replay 1280 720 $CASES > /dev/null 2>&1
  for c in $CASES; do for f in "$c"/out_*.rgba; do b=$(basename "$f" .rgba); mv "$f" "$c/feat${k}_${b#out_}.rgba"; done; done
done
build/cmake/fgx3_replay 1280 720 $CASES > /dev/null 2>&1   # restore the normal outputs
echo "$(echo "$CASES" | wc -l) cases"

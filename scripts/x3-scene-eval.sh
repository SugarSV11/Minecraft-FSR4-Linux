#!/usr/bin/env bash
# Replays a recorded scene through the real FG x3 pass and prints the quality report. Usage: x3-scene-eval.sh SCENE_DIR [STRIDE]
set -euo pipefail; cd "$(dirname "$0")/.."; D="$1"; rm -rf "$D/cases"; python3 scripts/x3_scene_dataset.py "$D" "$D/cases" "${2:-3}"
build/cmake/fgx3_replay 1280 720 $(cat "$D/cases/cases.txt") 2>&1 | grep -v WARNING | tail -1; python3 scripts/x3_scene_eval.py "$D/cases"

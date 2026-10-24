#!/usr/bin/env bash
# Replays every recorded scene through the current FG x3 shader and prints one compact summary per scene (PSNR dB / SSIM / gross-error % : generated | blend). Usage: x3-eval-all.sh [LABEL]
set -euo pipefail; cd "$(dirname "$0")/.."; . scripts/nc-env.sh; E="${EVID:-$(nc_evid 9)}"; LABEL="${1:-run}"
for s in baseline particles water vegetation transparency disocclusion animals; do [ -f "$E/$s/cases/cases.txt" ] || python3 scripts/x3_scene_dataset.py "$E/$s" "$E/$s/cases" >/dev/null
  build/cmake/fgx3_replay 1280 720 $(cat "$E/$s/cases/cases.txt") >/dev/null 2>&1; echo "######## $s"; python3 scripts/x3_scene_eval.py "$E/$s/cases" | tail -8; done > "docs/evidence/session9/scene_eval_$LABEL.txt"
python3 - "$LABEL" <<'PY'
import re,sys
t=open(f"docs/evidence/session9/scene_eval_{sys.argv[1]}.txt").read().split("########")[1:]
print(f"{'scene':14s} | phase A gen/blend PSNR  SSIM  gross% | phase B gen/blend PSNR  SSIM  gross%")
for blk in t:
    name=blk.split("\n")[0].strip(); out=[]
    for ph in "AB":
        m=re.search(rf"phase {ph}.*?generated PSNR\s+([\d.]+).*?SSIM ([\d.]+).*?gross-error px ([\d.]+).*?duplicate.*?blend\s+PSNR\s+([\d.]+) dB.*?SSIM ([\d.]+).*?gross-error px ([\d.]+)",blk,re.S)
        out.append(f"{m.group(1):>6s}/{m.group(4):>6s} {m.group(2)} {m.group(3):>6s}" if m else "-")
    print(f"{name:14s} | {out[0]:34s} | {out[1]}")
PY

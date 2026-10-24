#!/usr/bin/env bash
# neural-data.sh SCENE...: for each recorded scene (~/Escritorio/FSR4-evidence-session11/SCENE): features through the real generator, packs the uint8 dataset (tools/ai/neural/dataset.py) into ~/Escritorio/FSR4-neural-data, then removes the
# (large, regenerable) per-case directories. Needs ~4 GB of temporary disk per scene.
set -euo pipefail; cd "$(dirname "$0")/.."; . scripts/nc-env.sh; E=${E:-$(nc_evid 11)}; O=$NC_NEURAL; mkdir -p "$O"
for sc in "$@"; do
  rm -rf "$E/$sc/cases"; scripts/neural-features.sh "$E/$sc" 4 >/dev/null
  python3 tools/ai/neural/dataset.py "$O" "$sc"
  rm -rf "$E/$sc/cases"
  df -h /var/home | tail -1 | awk '{print "  free: " $4}'
done

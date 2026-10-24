#!/usr/bin/env bash
# neural-data-ab.sh SCENE...: scenes recorded with FSR4_PARTICLE_MV=1 FSR4_PARTICLE_AB=1 (scripts/x3-scene.sh, ~/Escritorio/FSR4-evidence-session13/SCENE): builds TWO feature datasets from the SAME recording and truth:
# SCENE_A = generator fed planes WITHOUT particle vectors (da_/ma_), SCENE_B = planes WITH particle vectors (d_/m_). Removes the per-case directories afterwards (~4 GB each).
set -euo pipefail; cd "$(dirname "$0")/.."; . scripts/nc-env.sh; E=${E:-$(nc_evid 13)}; O=$NC_NEURAL; mkdir -p "$O"
for sc in "$@"; do
  for v in a b; do
    pv=""; [ $v = a ] && pv=a
    rm -rf "$E/$sc/cases_$v"; PLANE_VARIANT=$pv CASES_DIR=cases_$v scripts/neural-features.sh "$E/$sc" 4 >/dev/null
    python3 -c "import sys; sys.path.insert(0,'tools/ai/neural'); import dataset as D; print('$sc'+'_${v^^}', D.build('$sc'+'_${v^^}', '$O', rec='$sc', cdir='cases_$v'), 'frames')"
    rm -rf "$E/$sc/cases_$v"; df -h /var/home | tail -1 | awk '{print "  free: " $4}'
  done
done

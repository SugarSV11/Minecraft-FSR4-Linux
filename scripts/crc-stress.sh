#!/usr/bin/env bash
# Reproduces the shared-plane self-test race: fresh helper per iteration under full CPU load (the failure appeared ~1 in 12 starts in the game, 4/60 here before the fix).
# Usage: crc-stress.sh [iterations=60] [load=1]
set -u; cd "$(dirname "$0")/.."; N="${1:-60}"; PIDS=()
if [ "${2:-1}" = 1 ]; then for i in $(seq 1 "$(nproc)"); do bash -c 'while :; do :; done' & PIDS+=($!); done; fi
FSR4_CRC_DIAG=1 timeout 1500 build/cmake/crc_stress restart "$N" 2>&1 | grep -v WARNING | tee /tmp/crc_stress_restart.log | tail -8
FSR4_CRC_DIAG=1 timeout 900 build/cmake/crc_stress realloc 300 2>&1 | grep -v WARNING | tail -3
for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done

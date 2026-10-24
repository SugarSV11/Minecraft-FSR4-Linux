#!/usr/bin/env bash
# Continuous-motion ghosting scene (deterministic by rendered-frame index, world tick frozen).
# Arena: black backdrop, oak-leaf cube, water basin, glass pane, one white sheep. Phase A (90 frames): static camera, sheep crosses at 0.06 blocks/frame.
# Phase B (90 frames): sheep parked, camera pans 0.3 deg/frame over leaves/water/glass. Usage: motion-test.sh MODE OUTDIR  (native|fsr315|fsr411)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; MODE="$1"; OUT="$2"; mkdir -p "$OUT"
case "$MODE" in native) EN=false; BE=FSR4_1_1;; fsr315) EN=true; BE=FSR3_1_5;; fsr411) EN=true; BE=FSR4_1_1;; *) echo bad mode; exit 2;; esac
CFG="$ROOT/fabric/run/config/fsr4linux.json"; cp "$CFG" "$OUT/cfg.orig"
echo "{\"enabled\": $EN, \"backend\": \"$BE\", \"quality\": \"${QUALITY:-QUALITY}\", \"sharpness\": 0.5, \"debugOverlay\": false, \"fpsCounter\": false, \"frameGeneration\": ${FG:-false}, \"fgPacing\": false, \"fgMultiplier\": ${FGMULT:-2}}" > "$CFG"
S="wait 40; cmd tp @s 300.5 152 300.5 0 8; fly; cmd time set noon; cmd kill @e[type=!player]; cmd fill 288 148 312 314 162 312 black_concrete; cmd fill 296 150 308 299 153 310 oak_leaves[persistent=true]"
S="$S; cmd fill 301 149 307 306 150 311 stone; cmd fill 302 150 308 305 150 310 water; cmd fill 291 150 308 295 153 308 light_blue_stained_glass; cmd kill @e[type=sheep]"
S="$S; cmd summon sheep 297.0 150 306.0 {NoAI:1b,Silent:1b,Color:0,Invulnerable:1b,NoGravity:1b}; wait 80; cmd tp @s 300.5 152 300.5 0 8; wait 20; clearchat; cmd tick freeze; wait 60; state; motion ${FA:-90} ${FB:-90} ${VEL:-0.06} ${YAW_RATE:-0.3} mo ${PITCH_RATE:-0} ${STRAFE:-0}; wait 5; cmd tick unfreeze; quit"
rm -f "$ROOT"/fabric/run/screenshots/mo_*.png "$ROOT"/fabric/run/screenshots/mog_*.png "$ROOT"/fabric/run/screenshots/x3g_*.png
FSR4_SCRIPT="$S" timeout 400 "$ROOT/scripts/run-client.sh" --width 1280 --height 720 >"$OUT/client.log" 2>&1 || true
mv "$ROOT"/fabric/run/screenshots/mo_*.png "$ROOT"/fabric/run/screenshots/mog_*.png "$ROOT"/fabric/run/screenshots/x3g_*.png "$OUT"/ 2>/dev/null || true
grep -E "STATE|motion" "$ROOT/fabric/run/fsr4-test.log" | tail -3 | cut -c1-240 > "$OUT/state.txt"
cp "$OUT/cfg.orig" "$CFG"; ls "$OUT" | grep -c '^mo_'

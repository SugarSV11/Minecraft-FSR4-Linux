#!/usr/bin/env bash
# Ghosting scene: a white sheep (NoAI) is teleported in 3 jumps in front of a black wall with the server tick frozen and a fixed camera.
# Each jump is followed by a burst of per-frame screenshots. Usage: scripts/ghost-test.sh MODE OUTDIR   (MODE = native|fsr315|fsr411)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; MODE="$1"; OUT="$2"; mkdir -p "$OUT"
case "$MODE" in
  native) EN=false; BE=FSR4_1_1;; fsr315) EN=true; BE=FSR3_1_5;; fsr411) EN=true; BE=FSR4_1_1;; *) echo bad mode; exit 2;;
esac
CFG="$ROOT/fabric/run/config/fsr4linux.json"; cp "$CFG" "$OUT/cfg.orig"
echo "{\"enabled\": $EN, \"backend\": \"$BE\", \"quality\": \"QUALITY\", \"sharpness\": 0.5, \"debugOverlay\": false, \"fpsCounter\": false, \"frameGeneration\": false, \"fgPacing\": false}" > "$CFG"
S="wait 40; cmd tp @s 300 142 300 0 0; fly; cmd fill 290 130 312 312 160 312 black_concrete; cmd kill @e[type=sheep]; cmd summon sheep 300 141 306 {NoAI:1b,Silent:1b,Color:0,Invulnerable:1b}; wait 60; cmd tp @s 300 142 300 0 0; wait 20; cmd tick freeze; wait 40; state"
x=300.0
for i in 0 1 2; do
  x=$(python3 -c "print(300.0+0.8*$i)")
  S="$S; burst g${i} 100; cmd tp @e[type=sheep,limit=1] $x 141 306; cmd tick step 1; wait 25"
done
S="$S; cmd tick unfreeze; quit"
rm -f "$ROOT"/fabric/run/screenshots/g[0-9]_*.png
FSR4_SCRIPT="$S" timeout 300 "$ROOT/scripts/run-client.sh" --width 1280 --height 720 >/dev/null 2>&1 || true
mv "$ROOT"/fabric/run/screenshots/g[0-9]_*.png "$OUT"/ 2>/dev/null || true
grep STATE "$ROOT/fabric/run/fsr4-test.log" | tail -1 | cut -c1-260 > "$OUT/state.txt"
cp "$OUT/cfg.orig" "$CFG"
ls "$OUT" | wc -l

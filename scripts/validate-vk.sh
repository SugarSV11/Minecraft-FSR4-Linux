#!/usr/bin/env bash
# Runs a short in-game session (FG x3/x4 batched presentation, FSR optional) with the Khronos validation layer + synchronization validation and prints every VUID / SYNC-HAZARD message.
# The layer is built locally (no root): ~/.cache/fsr4-linux/vvl/src/build/layers (see docs/PRESENTATION_ARCHITECTURE.md). Usage: validate-vk.sh MODE [steps]   MODE = native | fsr | fg3 | fg4 | fsr_fg3 | fsr_fg4
set -uo pipefail; cd "$(dirname "$0")/.."
MODE="${1:-fg3}"; LAYERS="${VVL_DIR:-$HOME/.cache/fsr4-linux/vvl/src/build/layers}"
FG=true
case "$MODE" in native) BE=NATIVE; EN=false; FG=false; M=2;; fsr) BE=FSR4_1_1; EN=true; FG=false; M=2;; fg3) BE=NATIVE; EN=false; M=3;; fg4) BE=NATIVE; EN=false; M=4;; fsr_fg3) BE=FSR4_1_1; EN=true; M=3;; fsr_fg4) BE=FSR4_1_1; EN=true; M=4;; *) echo bad mode; exit 2;; esac
printf '{\n "enabled": %s,\n "backend": "%s",\n "quality": "QUALITY",\n "sharpness": 0.5,\n "debugOverlay": false,\n "fpsCounter": false,\n "frameGeneration": %s,\n "fgPacing": false,\n "fgMultiplier": %s\n}\n' "$EN" "$BE" "$FG" "$M" > fabric/run/config/fsr4linux.json
STEPS="${2:-wait 60; cmd tp @s 300 140 300; wait 80; fly; look 30 20; wait 80; turn 1.5 60; wait 30; fullscreen; wait 60; fullscreen; wait 60; key fg; wait 60; key fg; wait 80; state; quit}"
OUT="${VAL_OUT:-/tmp/validate-$MODE.txt}"; rm -f fabric/run/fsr4-test.log
VK_LAYER_PATH="$LAYERS" VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT \
 VK_LAYER_DUPLICATE_MESSAGE_LIMIT=0 VK_LAYER_MESSAGE_ID_FILTER="${VAL_FILTER:-VUID-VkImageCreateInfo-pNext-00990,VUID-VkImportMemoryWin32HandleInfoKHR-handleType-09861}" FSR4_SCRIPT="wait 40; $STEPS" timeout 900 scripts/run-client.sh --width 1280 --height 720 > "$OUT" 2>&1; echo "exit=$?"
python3 - "$OUT" <<'PY'
import sys, re, collections
t = open(sys.argv[1], errors="replace").read()
msgs = re.split(r"\n(?=\[\d\d:\d\d:\d\d\] \[)", t)
c = collections.Counter(); own = 0
for m in msgs:
    if "(Minecraft)" not in m: continue
    hz = re.search(r"(vk\w+)\(\): (\w+) hazard detected", m); vu = re.search(r"(VUID-[\w-]+)", m)
    if hz:
        if "WriteBufferMarker2AMD" in m: c["(ignored: Minecraft's own checkpoint markers) " + hz.group(2)] += 1; continue
        c[f"SYNC {hz.group(1)} {hz.group(2)}"] += 1
    elif vu: c[vu.group(1) + " (" + (re.search(r"\] (vk\w+)\(\)", m) or re.search(r"(Object Tracking)", m) or re.search(r"(\w+)", m)).group(1) + ")"] += 1
print("--- validation findings (excluding Minecraft's checkpoint-marker hazards):")
for k, v in c.most_common(25): print(f"{v:6d}  {k}")
if not c: print("  none")
PY
grep -E "STATE" fabric/run/fsr4-test.log | tail -1 | cut -c1-200

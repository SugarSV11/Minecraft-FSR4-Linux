#!/usr/bin/env bash
# In-game regression: FSR 4.1.1 + FG x3 through F7-F10, quality/version changes, fullscreen, the settings menu (real click on the multiplier button), dimension changes,
# leaving and re-entering the world, and recovery from a killed Wine helper. Prints the state after every step group; exit 0 only if the script ran to the end.
set -u; cd "$(dirname "$0")/.."
echo '{"enabled": true, "backend": "FSR4_1_1", "quality": "QUALITY", "sharpness": 0.5, "debugOverlay": false, "fpsCounter": false, "frameGeneration": true, "fgPacing": false, "fgMultiplier": 3}' > fabric/run/config/fsr4linux.json
S="wait 40; cmd tp @s 300 140 300; wait 80; fly; look 30 20; wait 100; state; turn 1.5 100; key quality; wait 60; key version; wait 80; state; key version; wait 80; key fg; wait 60; state; key fg; wait 80; key toggle; wait 60; key toggle; wait 80; state; fullscreen; wait 80; state; fullscreen; wait 80; menu; wait 30; click Multiplicador; wait 40; state; click Multiplicador; wait 40; closemenu; wait 60; state; cmd execute in minecraft:the_nether run tp @s 0 100 0; wait 200; state; cmd execute in minecraft:overworld run tp @s 300 140 300; wait 200; state; leaveworld; wait 400; fly; look 30 20; wait 200; state; killhelper; wait 120; state; key toggle; wait 300; state; key fg; wait 300; state; shot reg; quit"
FSR4_SCRIPT="$S" timeout 1200 scripts/run-client.sh --width 1280 --height 720 > "${1:-/tmp/regression-x3.txt}" 2>&1; echo "exit=$?"
grep 'STATE' fabric/run/fsr4-test.log | sed 's/^[0-9]* //; s/(reported by the AMD runtime)//; s/executed=[^ ]* //' | cut -c1-215
grep -iE 'ERROR|exception' "${1:-/tmp/regression-x3.txt}" | grep -v Realms | head -8 | cut -c1-200
tr -d '\n ' < fabric/run/config/fsr4linux.json; echo

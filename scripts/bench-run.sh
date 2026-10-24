#!/usr/bin/env bash
# Reproducible in-game benchmark in the isolated profile. Usage: bench-run.sh OUT.csv WIDTH HEIGHT "BACKEND QUALITY ENABLED" "SCRIPT-STEPS" [renderDistance]
# Refuses to run while another load (stress-ng, cpu_bench) is active. Forces vsync off / no AFK limiter in the isolated profile only.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; OUT="$1"; W="$2"; H="$3"; read -r BE Q EN <<<"$4"; STEPS="$5"; RD="${6:-}"
busy() { pgrep -x -f "stress-ng|clpeak|glmark2|vkmark|furmark|cpu_bench.sh" >/dev/null || [ "$(cat /sys/class/drm/card1/device/gpu_busy_percent)" -gt 15 ] || [ "$(cut -d' ' -f1 /proc/loadavg | cut -d. -f1)" -ge 3 ]; }
if [ "${WAIT_IDLE:-0}" = 1 ]; then while busy; do sleep 10; done; fi
if busy; then echo "machine not idle (other benchmark/GPU load); abort or set WAIT_IDLE=1" >&2; exit 3; fi
L=$(cut -d' ' -f1 /proc/loadavg); echo "load=$L"
OPT="$ROOT/fabric/run/options.txt"
sed -i "s/^enableVsync:.*/enableVsync:${VSYNC:-false}/; s/^maxFps:.*/maxFps:260/; s/^inactivityFpsLimit:.*/inactivityFpsLimit:\"minimized\"/" "$OPT"
[ -n "$RD" ] && sed -i "s/^renderDistance:.*/renderDistance:$RD/; s/^graphicsPreset:.*/graphicsPreset:\"custom\"/" "$OPT"
[ "${KEEP_CONFIG:-0}" = 1 ] || printf '{\n  "enabled": %s,\n  "backend": "%s",\n  "quality": "%s",\n  "sharpness": 0.5,\n  "debugOverlay": false,\n  "fpsCounter": false,\n  "frameGeneration": false,\n  "fgPacing": %s\n}\n' "$EN" "$BE" "$Q" "${FG_PACING:-false}" > "$ROOT/fabric/run/config/fsr4linux.json"
for kv in graphicsPreset="fancy" renderDistance=16 improvedTransparency=false ${OPT_OVERRIDES:-}; do k="${kv%%=*}"; v="${kv#*=}"; if grep -q "^$k:" "$OPT"; then sed -i "s|^$k:.*|$k:$v|" "$OPT"; else echo "$k:$v" >> "$OPT"; fi; done
rm -f "$ROOT/fabric/run/fsr4-test.log" "$ROOT/fabric/run/bench.csv"
D=/sys/class/drm/card1/device; GPU_LOG="${OUT%.csv}.gpu"; : > "$GPU_LOG"
( set +e +o pipefail; J=""; while true; do [ -z "$J" ] && J=$(pgrep -f net.fabricmc.devlaunchinjector | head -1); echo "$(date +%s%3N) $(cat $D/gpu_busy_percent) $(( $(cat $D/mem_info_vram_used) / 1048576 )) $( [ -n "$J" ] && awk '{print $14+$15}' /proc/$J/stat 2>/dev/null || echo 0)"; sleep 0.2; done > "$GPU_LOG" ) & SAMPLER=$!
trap 'kill $SAMPLER 2>/dev/null' EXIT
FSR4_SCRIPT="wait 40; cmd /tp @s 300 140 300; wait 80; fly; look 30 20; wait 160; $STEPS; quit" timeout 900 "$ROOT/scripts/run-client.sh" --width "$W" --height "$H" > "${OUT%.csv}.log" 2>&1 || echo "exit=$?"
kill $SAMPLER 2>/dev/null || true; cp "$ROOT/fabric/run/bench.csv" "$OUT"; cp "$ROOT/fabric/run/fsr4-test.log" "${OUT%.csv}.testlog"; grep -E "^render|renderDistance" "$OPT" | head -2

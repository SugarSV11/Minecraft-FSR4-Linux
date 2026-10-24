#!/usr/bin/env bash
# blocks until no foreign benchmark/GPU load: no stress-ng/clpeak/..., gpu_busy <=15%, loadavg(1m) < 3
while pgrep -f "stress-ng|clpeak|glmark2|vkmark|furmark|cpu_bench.sh" | grep -qv "^$$\$" && pgrep -x "stress-ng|clpeak|glmark2|vkmark|furmark" >/dev/null || [ "$(cat /sys/class/drm/card1/device/gpu_busy_percent)" -gt 15 ] || [ "$(cut -d' ' -f1 /proc/loadavg | cut -d. -f1)" -ge 3 ]; do sleep 10; done
echo "idle: load=$(cut -d' ' -f1 /proc/loadavg) gpu=$(cat /sys/class/drm/card1/device/gpu_busy_percent)%"

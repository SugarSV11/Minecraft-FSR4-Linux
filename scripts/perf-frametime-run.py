#!/usr/bin/env python3
"""Controlled 1080p/VSync-off matrix in a new test profile; never opens user worlds."""

import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parent.parent
TAG = sys.argv[1] if len(sys.argv) > 1 else "base"
MODE = sys.argv[2] if len(sys.argv) > 2 else "matrix"
if MODE not in {"matrix", "motion", "focus"}:
    raise SystemExit("mode must be matrix, motion or focus")
MOTION = MODE == "motion"
if not TAG.replace("-", "").replace("_", "").isalnum():
    raise SystemExit("run tag must be alphanumeric")
PROFILE = ROOT / "build" / f"perf-{TAG}"
if PROFILE.exists():
    raise SystemExit(f"isolated profile already exists: {PROFILE}")
for proc in Path("/proc").iterdir():
    if not proc.name.isdigit():
        continue
    try:
        cmd = (proc / "cmdline").read_bytes().replace(b"\0", b" ")
    except OSError:
        continue
    if b"net.fabricmc.devlaunchinjector.Main" in cmd or b"net.minecraft.client.main.Main" in cmd:
        raise SystemExit(f"another Minecraft process is active (pid {proc.name}); benchmark cancelled")

gpu_busy = int(Path("/sys/class/drm/card1/device/gpu_busy_percent").read_text())
if gpu_busy > 15:
    raise SystemExit(f"GPU busy {gpu_busy}%; benchmark cancelled")

PROFILE.mkdir(parents=True)
(PROFILE / "config").mkdir()
options = (ROOT / "fabric/run/options.txt").read_text().splitlines()
overrides = {
    "enableVsync": "false", "preferredGraphicsBackend": '"vulkan"',
    "fullscreen": "false", "inactivityFpsLimit": '"minimized"',
    "pauseOnLostFocus": "false", "graphicsPreset": '"custom"',
    "renderDistance": "16", "simulationDistance": "12", "maxFps": "260",
}
seen = set()
for i, line in enumerate(options):
    key = line.partition(":")[0]
    if key in overrides:
        options[i] = f"{key}:{overrides[key]}"
        seen.add(key)
options.extend(f"{key}:{value}" for key, value in overrides.items() if key not in seen)
(PROFILE / "options.txt").write_text("\n".join(options) + "\n")
(PROFILE / "config/fsr4linux.json").write_text(json.dumps({
    "enabled": False, "backend": "NATIVE", "quality": "QUALITY", "sharpness": 0.5,
    "debugOverlay": False, "fpsCounter": False, "frameGeneration": False,
    "fgPacing": False, "fgMultiplier": 2,
}, indent=2) + "\n")

steps = ["wait 40", "cmd /tp @s 300 140 300", "wait 80", "fly", "look 30 20", "wait 100"]
def bench(name):
    if MOTION:
        steps.extend(["look 30 20", "sweeptime 30", "wait 40"])
    steps.extend(["state", f"bench {name}_a 150", f"bench {name}_b 150", f"bench {name}_c 150"])
    if MOTION:
        steps.append("sweepstop")

bench("native")
steps.extend(["key toggle", "wait 60"])
bench("fsr411q")
steps.extend(["key version", "wait 60"])
bench("fsr315q")
if MODE != "focus":
    steps.extend(["key fg", "wait 60"])
    bench("fsr315q_fg2")
    steps.extend(["key version", "wait 60"])
    bench("fsr411q_fg2")
    steps.extend(["key fg", "menu", "click Multiplicador", "closemenu", "key fg", "wait 60"])
    bench("fsr411q_fg3")
    steps.extend(["key fg", "menu", "click Multiplicador", "closemenu", "key fg", "wait 60"])
    bench("fsr411q_fg4")
    steps.extend(["key fg", "key quality", "wait 60"])
    bench("fsr411b")
    steps.extend(["key toggle", "key fg", "wait 60"])
    bench("native_fg4")
    steps.extend(["key fg", "menu", "click Multiplicador", "closemenu", "key fg", "wait 60"])
    bench("native_fg2")
    steps.extend(["key fg", "menu", "click Multiplicador", "closemenu", "key fg", "wait 60"])
    bench("native_fg3")
steps.append("quit")

env = os.environ.copy()
env.update({
    "FSR4_GAME_DIR": str(PROFILE),
    "FSR4_RUNTIME_DIR": str(ROOT / "build/runtime-isolated"),
    "FSR4_SCRIPT": ";".join(steps),
    "FSR4_GPU_TS": "1",
    "JVM_EXTRA": f"-Xlog:gc=info:file=build/perf-{TAG}/gc.log {os.environ.get('PERF_JVM_EXTRA', '')}",
})
print(f"benchmark {TAG}: {PROFILE}, GPU idle={gpu_busy}%, {len(steps)} steps", flush=True)
stop_sampling = threading.Event()
def sample_gpu():
    gpu = Path("/sys/class/drm/card1/device")
    with (PROFILE / "gpu.csv").open("w") as out:
        out.write("epoch_ms,busy_pct,temp_c,vram_mib\n")
        while not stop_sampling.is_set():
            try:
                busy = int((gpu / "gpu_busy_percent").read_text())
                temp = int((gpu / "hwmon/hwmon0/temp1_input").read_text()) / 1000
                vram = int((gpu / "mem_info_vram_used").read_text()) / 1048576
                out.write(f"{time.time_ns() // 1000000},{busy},{temp},{vram:.1f}\n")
                out.flush()
            except (OSError, ValueError):
                pass
            stop_sampling.wait(1)
thread = threading.Thread(target=sample_gpu, daemon=True)
thread.start()
try:
    with (PROFILE / "game.log").open("w") as log:
        result = subprocess.run([str(ROOT / "scripts/run-client.sh"), "--width", "1920", "--height", "1080"], cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=900)
finally:
    stop_sampling.set()
    thread.join(timeout=2)
print(f"game exit={result.returncode}; bench rows={sum(1 for _ in (PROFILE / 'bench.csv').open()) if (PROFILE / 'bench.csv').exists() else 0}", flush=True)
if result.returncode:
    raise SystemExit(result.returncode)

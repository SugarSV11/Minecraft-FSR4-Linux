#!/usr/bin/env bash
# Runs a Windows probe .exe under GE-Proton in the disposable prefix: run-win-probe.sh EXE LOGFILE [args...]; waits for completion.
set -euo pipefail
EXE="$(readlink -f "$1")"; LOG="$(readlink -f "$2")"; shift 2
export STEAM_COMPAT_DATA_PATH="${PROBE_PREFIX:-$HOME/.cache/fsr4-linux/pfx_ge}" STEAM_COMPAT_CLIENT_INSTALL_PATH="$HOME/.local/share/Steam" WINEDEBUG="${WINEDEBUG:--all}"
cd "$(dirname "$EXE")"
python3 "$HOME/.steam/steam/compatibilitytools.d/GE-Proton11-6-x86_64/proton" run "$EXE" "${1:-amd_fidelityfx_loader_dx12.dll}" "Z:$LOG" "${@:2}" >/dev/null 2>"$LOG.stderr" || true

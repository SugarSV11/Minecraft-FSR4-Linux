#!/usr/bin/env bash
# Stage only release runtime files. Proprietary AMD binaries are installed locally by the user.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${BETA1_DIST_DIR:-$ROOT/dist/minecraft-fsr4-1.0.0-beta.1}"
JAR="$ROOT/fabric/build/libs/fsr4linux-1.0.0-beta.1.jar"
LIB="$ROOT/build/lib/libfsr4linux.so"
HELPER="$ROOT/build/probe/fsr4_bridge.exe"
test -f "$JAR" && test -f "$LIB" && test -f "$HELPER"
if jar tf "$JAR" | grep -Ei '(neural|checkpoint|dataset|\.pt$|\.npy$|\.spv$|\.png$|\.ppm$)' ; then
  echo 'Research asset in Fabric JAR' >&2
  exit 1
fi
if nm -D "$LIB" | grep -i neural ; then
  echo 'Neural engine linked into the runtime library' >&2
  exit 1
fi
if strings "$JAR" "$LIB" "$HELPER" | grep -E '(NeuralCraft-Data|FSR4-neural-data|/run/media/system/steam)' ; then
  echo 'Private research path in release binary' >&2
  exit 1
fi
mkdir -p "$(dirname "$DEST")"
mkdir "$DEST" # fail if a package with this name already exists
install -Dm644 "$JAR" "$DEST/mods/$(basename "$JAR")"
install -Dm755 "$LIB" "$DEST/runtime/lib/libfsr4linux.so"
install -Dm755 "$HELPER" "$DEST/runtime/bridge/fsr4_bridge.exe"
install -Dm644 "$ROOT/LICENSE" "$DEST/LICENSE"
install -Dm644 "$ROOT/docs/BETA1_STATUS.md" "$DEST/BETA1_STATUS.md"
install -Dm644 "$ROOT/docs/GPU_COMPATIBILITY.md" "$DEST/GPU_COMPATIBILITY.md"
if find "$DEST" -type f | grep -Ei '(neural|checkpoint|dataset|\.pt$|\.npy$|\.spv$|\.png$|\.ppm$|\.dll$)' ; then
  echo 'Unexpected research or vendor asset in the release directory' >&2
  exit 1
fi
(cd "$DEST" && sha256sum mods/* runtime/lib/* runtime/bridge/* > SHA256SUMS)
printf 'Beta 1 package: %s\n' "$DEST"

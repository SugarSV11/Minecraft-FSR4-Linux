#!/usr/bin/env bash
# Installs the runtime pieces next to the user's data (NOT in the system): libfsr4linux.so, the Wine helper, and the AMD DLLs
# (taken from the official AMD FidelityFX SDK v2.3.0 release already downloaded into ~/.cache/fsr4-linux/prebuilt).
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/env.sh
DEST="${FSR4_RUNTIME_DIR:-$HOME/.local/share/fsr4linux}"
mkdir -p "$DEST/lib" "$DEST/bridge" "$DEST/prefix"
install -m755 build/lib/libfsr4linux.so "$DEST/lib/"
install -m755 build/probe/fsr4_bridge.exe "$DEST/bridge/"
for f in amd_fidelityfx_loader_dx12.dll amd_fidelityfx_upscaler_dx12.dll; do install -m644 "$FFX_PREBUILT_DIR/$f" "$DEST/bridge/"; done
# Frame Generation provider DLL (official, from the same SDK release; only loaded when F7 is used)
install -m644 "$FFX_SDK_DIR/Kits/FidelityFX/signedbin/amd_fidelityfx_framegeneration_dx12.dll" "$DEST/bridge/"
# FG 4.0.1 needs AMD's amdxcffx64.dll (driver component downloaded by GE-Proton's own verb, NOT redistributable) in the prefix's system32.
# Opt-in, local copy only: FSR4_COPY_AMDXCFFX=/path/to/amdxcffx64.dll (e.g. from another Proton prefix on this machine).
if [ -n "${FSR4_COPY_AMDXCFFX:-}" ] && [ -f "$FSR4_COPY_AMDXCFFX" ] && [ -d "$DEST/prefix/pfx/drive_c/windows/system32" ]; then cp -n "$FSR4_COPY_AMDXCFFX" "$DEST/prefix/pfx/drive_c/windows/system32/amdxcffx64.dll" && echo "copied amdxcffx64.dll into the runtime prefix"; fi
cp -n "$FFX_SDK_DIR/docs/license.md" "$DEST/bridge/AMD_FIDELITYFX_LICENSE.md" 2>/dev/null || true
echo "installed into $DEST:"; ls -l "$DEST/lib" "$DEST/bridge"

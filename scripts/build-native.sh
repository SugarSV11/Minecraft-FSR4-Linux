#!/usr/bin/env bash
# Builds the Linux library + tests (CMake/Ninja) and the Windows helper (mingw-w64 cross compile).
# Needs: ~/.cache/fsr4-linux/{FidelityFX-SDK,prebuilt,vk-headers} (see scripts/env.sh, docs/README).
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/env.sh
cmake -S . -B build/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_LIBRARY_OUTPUT_DIRECTORY="$PWD/build/lib"
cmake --build build/cmake
K="$FFX_SDK_DIR/Kits/FidelityFX"
mkdir -p build/probe
# Ubuntu/Debian ship two thread models: the -posix variant is the one with std::thread and friends.
MINGW_CXX=x86_64-w64-mingw32-g++
command -v x86_64-w64-mingw32-g++-posix >/dev/null 2>&1 && MINGW_CXX=x86_64-w64-mingw32-g++-posix
$MINGW_CXX -std=c++17 -O2 -static -mwindows -o build/probe/fsr4_bridge.exe native/backends/fsr4/bridge/fsr4_bridge.cpp \
  -Inative/include -I"$K/api/include" -I"$K/upscalers/include" -I"$K/framegeneration/include" -ld3d12 -ldxgi -luuid
cp -f "$FFX_PREBUILT_DIR"/amd_fidelityfx_{loader,upscaler,framegeneration}_dx12.dll build/probe/
ls -l build/lib/libfsr4linux.so build/probe/fsr4_bridge.exe
file build/lib/libfsr4linux.so | cut -c1-120

<div align="center">

# 🐧 Minecraft FSR 4.1.1 for Linux

### Official AMD FSR 4.1.1 / 3.1.5 in Minecraft Java (Fabric) on **Linux + AMD RADV**

![Minecraft](https://img.shields.io/badge/Minecraft-26.3-62B47A?style=for-the-badge&logo=minecraft&logoColor=white)
![Fabric](https://img.shields.io/badge/Fabric-0.19.5-DBD0B4?style=for-the-badge)
![GPU](https://img.shields.io/badge/GPU-AMD%20RADV-ED1C24?style=for-the-badge&logo=amd&logoColor=white)
![Linux](https://img.shields.io/badge/OS-Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)
![Status](https://img.shields.io/badge/status-experimental%20ALPHA-red?style=for-the-badge)
![License](https://img.shields.io/badge/code-MIT-blue?style=for-the-badge)

[🇪🇸 Español](README.md) · 🇬🇧 English

### 🪟 On Windows? → [FSR4.1-Minecraft-RDNA4](https://github.com/SugarSV11/FSR4.1-Minecraft-RDNA4)

</div>

---

> 🚧 **Experimental alpha.** This is the Linux version of the same mod as the [Windows version](https://github.com/SugarSV11/FSR4.1-Minecraft-RDNA4) (sibling repo). An alpha `.jar` is available in [Releases](https://github.com/SugarSV11/Minecraft-FSR4-Linux/releases/tag/v0.2.0-alpha) (built by GitHub Actions, not tested in game); you can also build it yourself.

## ✨ What does it do?

A Fabric mod for Minecraft Java 26.3 that upscales with **AMD's official FSR 4.1.1 / 3.1.5**. AMD's libraries are DirectX 12, so they run in a Wine helper (GE-Proton + VKD3D-Proton) that shares Vulkan textures with Minecraft's renderer through dma-buf.

| Feature | Status |
|---|---|
| FSR 4.1.1 | Verified in a real game with the AMD provider confirmed |
| FSR 3.1.5 | Automatic fallback |
| AMD Frame Generation ×2 | Official, via FSR FG API 4.0.1 |
| Frame Generation ×3 / ×4 | Experimental in-house backend (no AMD code) |

✅ **Tested on RX 9060 XT (Mesa RADV, Bazzite/Linux).** Other GPUs are only what the code anticipates.

## ⚠️ Limitations

1. 🐢 **FSR 4.1.1 is slower than native rendering** in the tested scene (CPU-bound Wine helper + IPC) — see [`docs/BENCHMARKS.md`](docs/BENCHMARKS.md).
2. 🎞️ **FG ×2 does not raise the displayed FPS** on RX 9060 XT with vanilla Minecraft (render thread is the bottleneck).
3. Needs GE-Proton + VKD3D-Proton installed separately; no standalone JAR.
4. AMD DLLs are **not redistributed** (proprietary licence): install them yourself with `install-runtime.sh`.
5. AMD RADV only; no Intel/NVIDIA. End-to-end latency not measured.

Full list: [`docs/KNOWN_LIMITATIONS.md`](docs/KNOWN_LIMITATIONS.md).

## ⌨️ Controls

| Key | Action |
|:---:|---|
| `F6` | Settings menu |
| `F7` | Toggle Frame Generation |
| `F8` | Switch FSR 3.1.5 ↔ 4.1.1 |
| `F9` | Cycle quality mode |
| `F10` | Toggle upscaling |

## 🚀 Build and install

Requirements: Linux + AMD RADV (Mesa 24.2+), JDK 25, CMake 3.20+, Ninja, GCC 13+/Clang 16+, mingw-w64 11+ (`-posix` variant; Ubuntu 24.04's works thanks to `native/include/d3d12_compat.h`, Fedora too), GE-Proton.

```bash
# one-time: external dependencies
mkdir -p ~/.cache/fsr4-linux && cd ~/.cache/fsr4-linux
git clone https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git
git -C FidelityFX-SDK checkout 60f4ea8
mkdir -p prebuilt vk-headers
cp FidelityFX-SDK/Kits/FidelityFX/signedbin/amd_fidelityfx_{loader,upscaler}_dx12.dll prebuilt/
ln -s /usr/include/vulkan vk-headers/vulkan

# build
git clone https://github.com/SugarSV11/Minecraft-FSR4-Linux.git && cd Minecraft-FSR4-Linux
source scripts/env.sh
./scripts/build-shaders.sh && ./scripts/build-native.sh && ./scripts/install-runtime.sh
cd fabric && ./gradlew build        # jar in fabric/build/libs/
```

Copy the jar into `mods/` (Minecraft 26.3 + Fabric Loader 0.19.5) and press `F6` in game.

## ✅ Tests

```bash
cd fabric && ./gradlew test         # 33 Java tests
```

Native tests (`prep_test`, `fgx3_test`, `ipc_test`, `gpu_ipc_test`) need a Vulkan device; see the Spanish README.

## 📜 Licences

Mod code: MIT. AMD FidelityFX SDK: AMD licence, **not included**. GE-Proton / VKD3D-Proton: LGPL-2.1+ / MIT, on the user's system.

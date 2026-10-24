# Auditoría de hardware y entorno (2026-10-11)

Todos los datos proceden de comandos ejecutados en esta máquina. Salida completa de `vulkaninfo` en `docs/evidence/vulkaninfo_full.txt`.

| Elemento | Valor |
|---|---|
| SO | Bazzite 44.20261002.0 (Kinoite, Fedora Atomic 44); imagen desplegada `ostree-image-signed:docker://ghcr.io/ublue-os/bazzite:stable` 44.20261006.1 |
| Kernel | 7.2.8-ogc5.1.fc44.x86_64 |
| GPU | AMD Radeon RX 9060 XT (Navi 44, gfx1200, RDNA 4), PCI `1002:7590` rev c0, subsistema Sapphire `1da2:e495`, 8 GB VRAM |
| Driver kernel | amdgpu (en uso), firmware `gc_12_0_0_*` presente |
| Mesa / RADV | Mesa 26.2.4 (`mesa-vulkan-drivers-26.2.4-1.fc44`), RADV `DRIVER_ID_MESA_RADV`, Vulkan device API 1.4.354, conformance 1.4.5.3, instancia Vulkan 1.4.341 (loader `vulkan-loader-1.4.341.0`) |
| GL | radeonsi 26.2.4, GL 4.6 core |
| CPU / RAM | Ryzen 7 5700 (16 hilos lógicos) / 15 GiB |
| Dispositivos Vulkan | GPU0 RADV RX 9060 XT; GPU1 llvmpipe (CPU, no usar) |

## Capacidades Vulkan relevantes (RADV)
- `VK_KHR_cooperative_matrix` (cooperativeMatrix = true), `VK_EXT_shader_float8` (shaderFloat8 y shaderFloat8CooperativeMatrix = true), `VK_KHR_shader_bfloat16`.
- shaderFloat16, shaderInt8, subgroupSizeControl; `maxComputeSharedMemorySize` 65536; subgroup 64 (wave64) y 32.
- Sincronización/memoria externa: `VK_KHR_external_memory(_fd)`, `VK_EXT_external_memory_dma_buf`, `VK_KHR_external_semaphore(_fd)`, `VK_KHR_synchronization2`, `VK_KHR_dynamic_rendering`, `VK_EXT_memory_budget`.
- Capas instaladas: gamescope WSI, MangoHud, Mesa anti-lag, device_select, OBS vkcapture, Korthos low-latency.

## Toolchain
| Herramienta | Estado |
|---|---|
| Java del anfitrión | OpenJDK 26.0.2 (Homebrew). **No es JDK 25.** El launcher de Minecraft usa su propio `java-runtime-epsilon` (Java 25). Se necesita un JDK 25 para compilar el mod (pendiente de resolver en toolbox/sdkman sin tocar el anfitrión). |
| Gradle | no instalado en el anfitrión (se usará el wrapper) |
| CMake 4.4.3, Ninja 1.13.2, Git 2.55.0, GCC/G++ 16.2.1, Clang 23.1.1 (Homebrew) | disponibles |
| mingw-w64 (g++ 16.2.1, capa rpm-ostree ya existente) | disponible; usado para compilar los prototipos Windows |
| glslc / glslangValidator / dxc | **no** en el anfitrión |
| Toolbox 0.3 | `fedora-toolbox-42` y `b3313-3ds-build` existen (parados) |
| Wine | wine-11.0 (Staging) del anfitrión; Proton Experimental `experimental-11.0-20261001`, GE-Proton11-5, GE-Proton11-6, Proton 10.0, UMU-Proton-10.0-4 |

## Minecraft
- Launchers presentes: Minecraft Launcher oficial (flatpak 2.1.3), Prism Launcher 11.1.1 (flatpak), Modrinth App 0.21.9 (flatpak).
- `~/.minecraft/versions`: `26.2`, `26.3-snapshot-7` (solo JSON, java-runtime-epsilon = Java 25, LWJGL 3.4.2), `fabric-loader-0.19.3-26.2`.
- `~/.minecraft/mods` está vacío. Fabric API presente solo para 26.2 (`~/Descargas/fabric-api-0.161.0+26.2.jar`).
- **No hay una versión "26.3" final instalada**: solo el snapshot 7. Ver `MINECRAFT_RENDERER_AUDIT.md`.

No se instaló ni modificó nada en el anfitrión. Todo lo descargado vive en `~/.cache/fsr4-linux/` y en el directorio del proyecto.

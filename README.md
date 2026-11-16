<div align="center">

# 🐧 Minecraft FSR 4.1.1 para Linux

### FSR 4.1.1 / 3.1.5 oficial de AMD en Minecraft Java (Fabric) sobre **Linux + AMD RADV**

![Minecraft](https://img.shields.io/badge/Minecraft-26.3-62B47A?style=for-the-badge&logo=minecraft&logoColor=white)
![Fabric](https://img.shields.io/badge/Fabric-0.19.5-DBD0B4?style=for-the-badge)
![GPU](https://img.shields.io/badge/GPU-AMD%20RADV-ED1C24?style=for-the-badge&logo=amd&logoColor=white)
![Linux](https://img.shields.io/badge/SO-Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)
![Estado](https://img.shields.io/badge/estado-ALPHA%20experimental-red?style=for-the-badge)
![Licencia](https://img.shields.io/badge/c%C3%B3digo-MIT-blue?style=for-the-badge)

🇪🇸 Español · [🇬🇧 English](README.en.md)

### 🪟 ¿Usas Windows? → [FSR4.1-Minecraft-RDNA4](https://github.com/SugarSV11/FSR4.1-Minecraft-RDNA4)

</div>

---

> 🚧 **Alpha experimental.** Es la versión Linux del mismo mod que la [versión Windows](https://github.com/SugarSV11/FSR4.1-Minecraft-RDNA4) (repo hermano). Aquí **hay que compilarlo tú**: no hay un `.jar` listo para instalar.

## ✨ ¿Qué hace?

Mod Fabric para Minecraft Java 26.3 que escala la imagen con **FSR 4.1.1 / 3.1.5 oficial de AMD**. Como las librerías de AMD son DirectX 12, corren en un *helper* Wine (GE-Proton + VKD3D-Proton) que comparte texturas Vulkan por dma-buf con el renderizador de Minecraft.

| Función | Estado |
|---|---|
| 🔍 **FSR 4.1.1** | Verificado en juego real con proveedor AMD confirmado |
| 🔁 **FSR 3.1.5** | Fallback automático si 4.1.1 no está disponible |
| 🎞️ **Frame Generation AMD ×2** | Oficial, vía FSR FG API 4.0.1 |
| 🧪 **Frame Generation ×3 / ×4** | Backend experimental propio (sin código de AMD) |
| 💾 **Configuración** | JSON persistente en `config/fsr4linux.json` |

✅ **Probado en RX 9060 XT (Mesa RADV, Bazzite/Linux).** La compatibilidad con otras GPUs es solo la prevista por el código ([ver matriz](#-compatibilidad-gpu)).

## ⚠️ Limitaciones (léelas antes de instalar)

| # | Limitación | Detalle |
|:-:|---|---|
| 1 | 🐢 **FSR 4.1.1 es más lento que el render nativo** en la escena probada | Cuello de botella de CPU en el helper Wine + IPC. Ver [`docs/BENCHMARKS.md`](docs/BENCHMARKS.md) |
| 2 | 🎞️ **FG ×2 no sube los FPS mostrados** en RX 9060 XT con Minecraft vanilla | El hilo de render es el cuello de botella |
| 3 | 🧩 Requiere GE-Proton + VKD3D-Proton instalados aparte | No hay JAR 100 % autónomo |
| 4 | 📦 Las DLL de AMD **no se redistribuyen** | Licencia propietaria: las pones tú vía `install-runtime.sh` |
| 5 | 🖥️ Solo AMD con RADV | No hay soporte Intel/NVIDIA |
| 6 | 🧠 Corrector neuronal/desoclusión: **solo investigación offline** | Ver `docs/NEURAL_ACCEPTANCE.md` |
| 7 | ⏱️ Latencia extremo a extremo **no medida** | Solo hay estimaciones por software |
| 8 | 📺 Un monitor de 60 Hz limita los FPS presentados visibles | Las métricas cuentan `vkQueuePresentKHR` |

Lista completa: [`docs/KNOWN_LIMITATIONS.md`](docs/KNOWN_LIMITATIONS.md).

## ⌨️ Controles

| Tecla | Acción |
|:---:|---|
| `F6` | Abrir el menú de ajustes (también en Opciones › Video › «FSR…») |
| `F7` | Activar / desactivar Frame Generation |
| `F8` | Cambiar versión: FSR 3.1.5 ↔ 4.1.1 |
| `F9` | Cambiar calidad: Ultra Quality → Quality → Balanced → Performance → Ultra Performance |
| `F10` | Activar / desactivar el escalado |

La configuración se guarda sola en `config/fsr4linux.json`.

## 🚀 Instalación paso a paso

### 1. Requisitos

| Herramienta | Versión mínima | Para qué |
|---|---|---|
| Linux + GPU AMD con RADV | Mesa 24.2+ | Vulkan 1.3 con `VK_KHR_external_memory_fd`, `VK_KHR_external_semaphore_fd`, `VK_KHR_timeline_semaphore` |
| JDK | 25 | Compilar el mod (Loom) |
| CMake + Ninja | 3.20 / 1.11 | Biblioteca nativa |
| GCC o Clang | 13 / 16 | C++20 |
| mingw-w64 | con cabeceras D3D12 recientes (`ID3D12Device3`) | Compilar el helper de Windows (`fsr4_bridge.exe`). El mingw-w64 11.0.1 de Ubuntu 24.04 **no basta**; el proyecto se desarrolló con la toolbox de Fedora 42 |
| GE-Proton | reciente | Wine + VKD3D-Proton, en `~/.steam/steam/compatibilitytools.d/` |

### 2. Dependencias externas (una sola vez)

```bash
mkdir -p ~/.cache/fsr4-linux && cd ~/.cache/fsr4-linux

# FidelityFX SDK v2.3.0 (commit 60f4ea8)
git clone https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git
git -C FidelityFX-SDK checkout 60f4ea8

# DLL oficiales de AMD (del SDK) y headers de Vulkan
mkdir -p prebuilt vk-headers
cp FidelityFX-SDK/Kits/FidelityFX/signedbin/amd_fidelityfx_{loader,upscaler}_dx12.dll prebuilt/
ln -s /usr/include/vulkan vk-headers/vulkan     # o clona KhronosGroup/Vulkan-Headers
```

### 3. Compilar

```bash
git clone https://github.com/SugarSV11/Minecraft-FSR4-Linux.git
cd Minecraft-FSR4-Linux
source scripts/env.sh

./scripts/build-shaders.sh     # shaders -> SPIR-V (necesita glslc; los .h ya generados vienen en el repo)
./scripts/build-native.sh      # build/lib/libfsr4linux.so + build/probe/fsr4_bridge.exe
./scripts/install-runtime.sh   # instala en ~/.local/share/fsr4linux/
cd fabric && ./gradlew build   # el .jar queda en fabric/build/libs/
```

### 4. Instalar el mod

1. Instala Minecraft 26.3 con **Fabric Loader 0.19.5**.
2. Copia el `.jar` de `fabric/build/libs/` a tu carpeta `mods/`.
3. Abre el juego y pulsa `F6` para ajustar.

> **¿Por qué no hay un JAR autónomo?** Las DLL de AMD tienen licencia propietaria, el helper necesita GE-Proton instalado en el sistema y la biblioteca nativa carga `libvulkan.so.1` del driver.

## ✅ Tests

```bash
# Java (33 tests)
cd fabric && ./gradlew test

# Nativos (necesitan un dispositivo Vulkan)
cmake -S . -B build/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake --target prep_test fgx3_test ipc_test gpu_ipc_test
./build/cmake/prep_test && ./build/cmake/fgx3_test && ./build/cmake/ipc_test && ./build/cmake/gpu_ipc_test
```

Validación Vulkan (Khronos + sincronización): `./scripts/validate-vk.sh fsr_fg4`.
Resultados registrados: [`docs/TEST_RESULTS.md`](docs/TEST_RESULTS.md).

## 🖥️ Compatibilidad GPU

> Esta tabla es la **prevista según el código** (detección de GFX IP y lógica de fallback). **Solo se ha probado en RX 9060 XT.**

| Arquitectura | FSR 4.1.1 | FSR 3.1.5 | FG ×2 (AMD) | FG ×3/×4 (propio) |
|---|:---:|:---:|:---:|:---:|
| RDNA 4 (RX 9000) | ✅ | ✅ | ✅ | ✅ |
| RDNA 3 (RX 7000) | según SDK | ✅ | ✅ | ✅ |
| RDNA 3.5 (APU 8000) | ❌ | ✅ | ✅ | ✅ |
| RDNA 2 (RX 6000) | ❌ | ✅ | ✅ | ✅ |
| RDNA 1 (RX 5000) | ❌ | ✅ | ⚠️ limitado | ✅ |

Detalles en [`docs/GPU_COMPATIBILITY.md`](docs/GPU_COMPATIBILITY.md).

## 🏗️ Arquitectura

```text
Hilo de render de Minecraft                 Helper Wine (fsr4_bridge.exe)
┌──────────────────────────┐               ┌──────────────────────────────┐
│ 1. Render a resolución   │   dma-buf     │ 2. FFX Upscaler (DX12)       │
│    interna + prep pass   │ ───────────►  │    FSR 4.1.1 / 3.1.5         │
│    (profundidad + MV)    │ imágenes VK   │    auto-exposición, nitidez  │
│ 3. Copia al render final │ ◄───────────  │ 3. Salida en imagen compartida│
└──────────────────────────┘               └──────────────────────────────┘
          ▲            IPC (socket Unix + memoria compartida)   │
          └─────────────────────────────────────────────────────┘
```

- **FG ×2 (AMD):** mismo helper, FSR FG API 4.0.1.
- **FG ×3/×4 (propio):** compute shader Vulkan (`fgx3.comp`) en el dispositivo de Minecraft; presentación por lotes (`BatchPresent`).
- **Sincronización:** por defecto espera de CPU; el fence compartido es experimental. Un *watchdog* libera esperas si el helper muere.

## 📂 Estructura del repositorio

```text
Minecraft-FSR4-Linux/
├── fabric/      # Mod Fabric (Java 25): core/ (lógica) y client/ (mixins, runtime, binding nativo)
├── native/      # Biblioteca C++20 libfsr4linux.so + helper de Windows (backends/fsr4)
├── shaders/     # Shaders Vulkan compute (.comp)
├── scripts/     # Compilación, tests, benchmarks y empaquetado
├── tests/       # Tests nativos C++
├── docs/        # Documentación técnica (STATUS, BENCHMARKS, KNOWN_LIMITATIONS…)
├── CMakeLists.txt
└── LICENSE
```

<details>
<summary>🔧 Variables de entorno (debug / experimental)</summary>

| Variable | Efecto |
|---|---|
| `FSR4_RUNTIME_DIR` | Directorio del runtime (por defecto `~/.local/share/fsr4linux`) |
| `FSR4_PROTON` | Ruta a un GE-Proton concreto |
| `FSR4_STEAM_DIR` | Directorio Steam personalizado |
| `FSR4_SYNC=cpu` / `gpu` | Sincronización por CPU (por defecto) / fence compartido (experimental) |
| `FSR4_GPU_TS=1` | Timestamps de GPU |
| `FSR4_NOWAIT=1` | Experimento: no esperar la salida del upscaler (imagen incorrecta) |
| `FSR4_X3_VARIANT=N` | Variante del shader FG ×3/×4 (0-7, por defecto 2) |
| `FSR4_PARTICLE_MV=1` | Vectores de movimiento por partícula (experimental) |
| `FSR4_ENTITY_MV=0` | Desactiva vectores por entidad (activos por defecto) |
| `FSR4_SCRIPT="..."` | TestHarness automatizado (ver `scripts/run-client.sh`) |

</details>

## 📜 Licencias y créditos

| Componente | Licencia |
|---|---|
| Código del mod (Java, C++, shaders, scripts, docs) | MIT ([`LICENSE`](LICENSE)) |
| AMD FidelityFX SDK | Licencia de AMD FidelityFX — **no incluido** (descarga aparte) |
| GE-Proton / VKD3D-Proton | LGPL-2.1+ / MIT — en el sistema del usuario |
| Fabric API / Loom / Minecraft | Sus licencias respectivas (dependencias de Gradle) |

Gracias a **AMD** (FidelityFX), **GloriousEggroll** (GE-Proton), **FabricMC** y **Khronos / Mesa / RADV**.

<div align="center">

🪟 ¿Windows? → [FSR4.1-Minecraft-RDNA4](https://github.com/SugarSV11/FSR4.1-Minecraft-RDNA4)

</div>

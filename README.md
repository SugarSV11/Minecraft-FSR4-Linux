# Minecraft FSR 4.1.1 for Linux (Fabric)

Mod Fabric para Minecraft Java 26.3 (Bazzite/Linux, AMD RADV) que implementa escalado temporal usando **FSR 4.1.1 / 3.1.5 oficial de AMD**, ejecutado en un helper Wine (GE-Proton + VKD3D-Proton) y compartiendo texturas Vulkan por dma-buf con el renderizador de Minecraft.

## Estado Actual

- **FSR 4.1.1**: Verificado en juego real con proveedor AMD confirmado
- **FSR 3.1.5**: Fallback automático cuando 4.1.1 no está disponible
- **Frame Generation AMD ×2**: Oficial, a través de FSR FG API 4.0.1
- **Frame Generation ×3/×4**: Backend experimental propio (sin código AMD)
- **Controles**: F6=menú, F7=FG, F8=versión, F9=calidad, F10=on/off
- **Configuración persistente**: JSON en `config/fsr4linux.json`
- **Compatibilidad**: Solo Linux, AMD RDNA 1–4 (ver GPU_COMPATIBILITY.md)

> **Nota**: Rendimiento FSR 4.1.1 aún más lento que nativo en la escena probada (ver BENCHMARKS.md). FG ×2 no aumenta FPS presentados en RX 9060 XT con Minecraft vanilla.

---

## Requisitos de Compilación

### Sistema
- **Linux** (probado en Bazzite KDE, kernel 6.x+)
- **GPU AMD Radeon** con driver RADV (Mesa 24.2+)
- **Vulkan 1.3** con extensiones: `VK_KHR_external_memory_fd`, `VK_KHR_external_semaphore_fd`, `VK_KHR_timeline_semaphore`

### Dependencias de Build
| Herramienta | Versión Mínima | Propósito |
|-------------|----------------|-----------|
| JDK | 25 (Java 25) | Compilación Java (Loom lo descarga) |
| Gradle | Wrapper incluido | Build del mod Fabric |
| CMake | 3.20+ | Build nativo (libfsr4linux.so) |
| Ninja | 1.11+ | Generador CMake recomendado |
| GCC/Clang | 13+ / 16+ | C++20 para código nativo |
| mingw-w64 | 11+ | Cross-compile helper Windows (fsr4_bridge.exe) |
| Vulkan Headers | 1.3.280+ | Solo headers (loader se usa vía dlopen) |

### Dependencias de Runtime (Usuario Final)
| Componente | Fuente | Notas |
|------------|--------|-------|
| **GE-Proton** | `~/.steam/steam/compatibilitytools.d/GE-Proton*` | Wine patched para VKD3D-Proton |
| **VKD3D-Proton** | Incluido en GE-Proton ≥ 3.0 | Traduce DX12→Vulkan para AMD DLLs |
| **AMD FidelityFX DLLs** | SDK oficial v2.3.0 | `amd_fidelityfx_loader_dx12.dll`, `amd_fidelityfx_upscaler_dx12.dll` — **NO se redistribuyen** |

---

## Estructura del Repositorio

```
Minecraft-FSR4-Source-Code/
├── fabric/                    # Mod Fabric (Java 25)
│   ├── src/
│   │   ├── main/java/dev/fsr4linux/
│   │   │   ├── core/          # Lógica upscaler, config, backends, fallback
│   │   │   └── client/        # Entry point, runtime, mixins, native binding
│   │   ├── client/resources/  # Mixins, lang, assets
│   │   └── test/java/         # Tests unitarios (23 tests)
│   ├── build.gradle           # Config Loom, Java 25, maven-publish
│   ├── gradle.properties      # Versiones: MC 26.3, Loader 0.19.5, Fabric API 0.162.0
│   └── settings.gradle
├── native/                    # Biblioteca nativa C++20 (libfsr4linux.so)
│   ├── src/                   # Implementación: bridge_client, gpu_bridge, prep_pass, fgx3_pass, fsr4_capi
│   ├── include/               # Headers públicos (fsr4_capi.h, fgx3_pass.hpp, etc.)
│   ├── generated/             # SPIR-V embebido (shaders compilados)
│   └── backends/fsr4/         # Helper Windows + probes experimentales
├── shaders/                   # Shaders Vulkan compute (.comp)
│   ├── fsr4_prep.comp         # Prep pass: depth + motion vectors
│   ├── fgx3.comp              # FG ×3/×4: generación de frames intermedios
│   └── neural_sel.comp        # Experimental (no integrado)
├── scripts/                   # Build, test, benchmark, packaging
│   ├── build-native.sh        # Compila libfsr4linux.so + fsr4_bridge.exe
│   ├── build-shaders.sh       # Compila .comp → SPIR-V headers
│   ├── install-runtime.sh     # Instala runtime local (~/.local/share/fsr4linux)
│   ├── package-beta1.sh       # Crea paquete de release (JAR + runtime)
│   ├── validate-vk.sh         # Validación Vulkan Khronos + sync
│   └── run-client.sh          # Lanza Minecraft con classpath correcto
├── tests/                     # Tests nativos C++ (prep, fgx3, ipc, gpu_ipc, crc_stress)
├── benchmarks/                # Resultados históricos (CSV, logs, .md)
├── docs/                      # Documentación técnica completa
│   ├── STATUS.md              # Estado por sesión, auditorías
│   ├── BENCHMARKS.md          # Métricas de rendimiento
│   ├── GPU_COMPATIBILITY.md   # Matriz AMD RDNA 1–4, fallbacks
│   ├── FRAME_GENERATION.md    # Detalles FG ×2/×3/×4
│   ├── KNOWN_LIMITATIONS.md   # Limitaciones conocidas y no resueltas
│   ├── TEST_RESULTS.md        # Resultados de tests
│   └── evidence/              # Evidencia cruda (logs, CSV, capturas)
├── CMakeLists.txt             # Build nativo (libfsr4linux.so + tests)
├── LICENSE                    # MIT License
└── uncommitted-changes-*.patch # Cambios sin commit de ramas originales
```

---

## Compilación Paso a Paso

### 1. Preparar Dependencias Externas (una sola vez)

```bash
# Directorio de trabajo para SDKs no versionados
mkdir -p ~/.cache/fsr4-linux

# 1. FidelityFX SDK v2.3.0 (commit 60f4ea8)
cd ~/.cache/fsr4-linux
git clone https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git
cd FidelityFX-SDK && git checkout 60f4ea8

# 2. Prebuilt AMD DLLs (del SDK o release oficial)
mkdir -p ~/.cache/fsr4-linux/prebuilt
cp FidelityFX-SDK/prebuilt/x64/amd_fidelityfx_{loader,upscaler}_dx12.dll ~/.cache/fsr4-linux/prebuilt/

# 3. Vulkan Headers (para CMake)
mkdir -p ~/.cache/fsr4-linux/vk-headers
# Opción A: Desde SDK Vulkan
ln -s /usr/include/vulkan ~/.cache/fsr4-linux/vk-headers/vulkan
# Opción B: Descargar headers específicos
# git clone https://github.com/KhronosGroup/Vulkan-Headers.git ~/.cache/fsr4-linux/vk-headers

# 4. GE-Proton (runtime Wine)
# Instalar desde https://github.com/GloriousEggroll/proton-ge-custom/releases
# Extraer en ~/.steam/steam/compatibilitytools.d/GE-Proton-X-XX
```

### 2. Compilar Shaders (SPIR-V)

```bash
cd Minecraft-FSR4-Source-Code
source scripts/env.sh
./scripts/build-shaders.sh
# Genera: native/generated/*.h con arrays SPIR-V embebidos
```

### 3. Compilar Biblioteca Nativa + Helper Windows

```bash
cd Minecraft-FSR4-Source-Code
source scripts/env.sh
./scripts/build-native.sh
# Genera:
#   build/lib/libfsr4linux.so          (Linux shared library)
#   build/probe/fsr4_bridge.exe        (Windows helper, mingw-w64)
```

**Verificación:**
```bash
file build/lib/libfsr4linux.so
# Debe mostrar: ELF 64-bit LSB shared object, x86-64

file build/probe/fsr4_bridge.exe
# Debe mostrar: PE32+ executable (GUI) x86-64, for MS Windows
```

### 4. Instalar Runtime Local (para desarrollo/testing)

```bash
cd Minecraft-FSR4-Source-Code
./scripts/install-runtime.sh
# Instala en ~/.local/share/fsr4linux/:
#   lib/libfsr4linux.so
#   bridge/fsr4_bridge.exe
#   bridge/amd_fidelityfx_loader_dx12.dll
#   bridge/amd_fidelityfx_upscaler_dx12.dll
```

### 5. Compilar Mod Fabric (JAR)

```bash
cd Minecraft-FSR4-Source-Code/fabric
./gradlew build
# Genera: build/libs/fsr4linux-1.0.0-beta.1.jar
```

### 6. Ejecutar Tests

```bash
# Tests Java (23 tests)
cd fabric && ./gradlew test

# Tests Nativos (requieren Vulkan device)
cd .. && source scripts/env.sh
cmake -S . -B build/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake --target prep_test fgx3_test ipc_test gpu_ipc_test
./build/cmake/prep_test
./build/cmake/fgx3_test
./build/cmake/ipc_test
./build/cmake/gpu_ipc_test
```

---

## Instalación para Usuario Final (JAR Autónomo)

El JAR generado **requiere** que el usuario tenga instalado localmente:

1. **GE-Proton** en `~/.steam/steam/compatibilitytools.d/GE-Proton*`
2. **AMD DLLs** en `~/.local/share/fsr4linux/bridge/` (via `install-runtime.sh`)
3. **libfsr4linux.so** en `~/.local/share/fsr4linux/lib/` (via `install-runtime.sh`)

> **No es posible un JAR 100% autónomo** porque:
> - Las DLLs de AMD FidelityFX tienen licencia propietaria (no redistribuible)
> - El helper Wine (fsr4_bridge.exe) necesita GE-Proton + VKD3D-Proton instalados en el sistema
> - La biblioteca nativa usa `dlopen("libvulkan.so.1")` vinculada al driver del sistema

**Instalación rápida para el usuario:**
```bash
# 1. Instalar GE-Proton (una vez)
# 2. Clonar este repo y compilar, O descargar release empaquetado
# 3. Instalar runtime local:
./scripts/install-runtime.sh

# 4. Copiar JAR a mods/
cp fabric/build/libs/fsr4linux-1.0.0-beta.1.jar ~/.minecraft/mods/

# 5. Iniciar Minecraft con Fabric Loader 0.19.5 + MC 26.3
```

---

## Controles en Juego

| Tecla | Acción |
|-------|--------|
| **F6** | Abrir menú de ajustes (también en Opciones > Video > "FSR...") |
| **F7** | Alternar Frame Generation (×2 AMD / ×3/×4 propio) |
| **F8** | Ciclar versión: FSR 3.1.5 ↔ FSR 4.1.1 |
| **F9** | Ciclar calidad: ULTRA_QUALITY → QUALITY → BALANCED → PERFORMANCE → ULTRA_PERFORMANCE |
| **F10** | Activar/desactivar upscaling |

**Persistencia**: Configuración en `config/fsr4linux.json` (se guarda automáticamente).

---

## Compatibilidad GPU (Resumen)

| Arquitectura | FSR 4.1.1 | FSR 3.1.5 | FG ×2 (AMD) | FG ×3/×4 (Propio) |
|--------------|-----------|-----------|-------------|-------------------|
| **RDNA 4** (RX 9000) | ✅ Nativo | ✅ Fallback | ✅ | ✅ |
| **RDNA 3** (RX 7000) | ✅ Si SDK lo permite | ✅ Fallback | ✅ | ✅ |
| **RDNA 3.5** (APU 8000) | ❌ | ✅ | ✅ | ✅ |
| **RDNA 2** (RX 6000) | ❌ | ✅ | ✅ | ✅ |
| **RDNA 1** (RX 5000) | ❌ | ✅ | ⚠️ Limitado | ✅ |

Ver `docs/GPU_COMPATIBILITY.md` para detalles completos, detección de GFX IP y lógica de fallback.

---

## Arquitectura Técnica

### Pipeline de Escalado (Upscaling)
```
Minecraft Render Thread                    Wine Helper Process (fsr4_bridge.exe)
┌─────────────────────────┐                ┌─────────────────────────────────┐
│ 1. World Pass           │                │                                 │
│    - Render a resolución│                │                                 │
│      interna (Quality)  │                │                                 │
│    - Prep Pass: depth + │  dma-buf       │ 2. FFX Upscaler (DX12)          │
│      motion vectors     │ ──────────►    │    - FSR 4.1.1 / 3.1.5          │
│    - Hand Pass: color   │  Vulkan images │    - Auto-exposure, sharpening  │
│                         │                │                                 │
│ 3. Copy-out → Main RT   │ ◄──────────── │ 3. Output → shared image        │
└─────────────────────────┘   dma-buf      └─────────────────────────────────┘
        ▲                                                    │
        │                    IPC (Unix socket + shared mem)  │
        └────────────────────────────────────────────────────┘
```

### Frame Generation
- **FG ×2 (AMD)**: Mismo helper, FSR FG API 4.0.1, presenta frame generado + frame real (pacing opcional)
- **FG ×3/×4 (Propio)**: Compute shader Vulkan (`fgx3.comp`) en el device de Minecraft, presentación por lotes (`BatchPresent`: 1 submit/frame)

### Sincronización GPU-GPU
- **Fence compartido** (timeline semaphore): implementación conservada para diagnóstico explícito. La ruta normal usa espera CPU mientras no haya una transferencia verificable del descriptor de fence de Wine a Vulkan.
- **Fallback CPU**: `vkQueueWaitIdle` + timeline semaphore local si `VK_KHR_external_semaphore_fd` no disponible
- **Watchdog**: Detecta helper muerto/bloqueado y libera GPU waits atascados

---

## Variables de Entorno (Debug/Experimental)

| Variable | Efecto |
|----------|--------|
| `FSR4_RUNTIME_DIR` | Override directorio runtime (default `~/.local/share/fsr4linux`) |
| `FSR4_PROTON` | Path a GE-Proton específico |
| `FSR4_STEAM_DIR` | Directorio Steam personalizado |
| `FSR4_SYNC=cpu` | Sincronización CPU-wait (actualmente predeterminada) |
| `FSR4_SYNC=gpu` | Probar el fence compartido de forma explícita; diagnóstico experimental |
| `FSR4_GPU_TS=1` | Habilitar timestamps GPU (requiere `VK_KHR_timeline_semaphore`) |
| `FSR4_NOWAIT=1` | Experimento: no esperar output upscaler (imagen incorrecta, mide overlap) |
| `FSR4_X3_VARIANT=N` | Variant shader FG ×3/×4 (0-7, default 2 = medido mejor) |
| `FSR4_X3_NO_INT64=1` | Desactivar shader int64 atómico single-pass |
| `FSR4_PARTICLE_MV=1` | Opt-in: vectores de movimiento por partícula (experimental) |
| `FSR4_ENTITY_MV=0` | Desactivar vectores por entidad (on por defecto) |
| `FSR4_PRESENT_PROBE=1` | Probe de presentación (VK_KHR_present_wait) |
| `FSR4_SCRIPT="..."` | Activar TestHarness automatizado (ver `scripts/run-client.sh`) |

---

## Tests y Validación

### TestHarness Automatizado
```bash
# Ejemplo: benchmark 200 frames con FSR 4.1.1 Quality + FG ×4
FSR4_SCRIPT="wait 60; bench fsr_fg4 200; state; quit" \
  ./scripts/run-client.sh --width 1920 --height 1080
```
Genera: `fsr4-test.log`, `bench.csv`, `frametime-samples.csv`

### Validación Vulkan (Khronos + Synchronization)
```bash
# Modos: native | fsr | fg3 | fg4 | fsr_fg3 | fsr_fg4
./scripts/validate-vk.sh fsr_fg4
```
Output: `/tmp/validate-fsr_fg4.txt` con VUIDs y SYNC-HAZARDs.

---

## Empaquetado de Release (Beta 1)

```bash
cd Minecraft-FSR4-Source-Code
BETA1_DIST_DIR=~/dist ./scripts/package-beta1.sh
# Crea: ~/dist/minecraft-fsr4-1.0.0-beta.1/
#   ├── mods/fsr4linux-1.0.0-beta.1.jar
#   ├── runtime/lib/libfsr4linux.so
#   ├── runtime/bridge/fsr4_bridge.exe
#   ├── LICENSE
#   ├── BETA1_STATUS.md
#   ├── GPU_COMPATIBILITY.md
#   └── SHA256SUMS
```

**Checks de seguridad en package-beta1.sh:**
- No assets de investigación (neural, checkpoints, datasets, .spv, .png) en JAR
- No símbolos neural en libfsr4linux.so
- No paths privados en binarios
- No DLLs AMD en release (usuario las instala localmente)

---

## Documentación Técnica Clave

| Documento | Contenido |
|-----------|-----------|
| `docs/STATUS.md` | Historial por sesión, auditorías, estado actual |
| `docs/BENCHMARKS.md` | Métricas FPS, GPU ms, presentados/s por modo |
| `docs/GPU_COMPATIBILITY.md` | Matriz RDNA 1–4, GFX IP, lógica fallback |
| `docs/FRAME_GENERATION.md` | Arquitectura FG ×2/×3/×4, pacing, present batch |
| `docs/KNOWN_LIMITATIONS.md` | Limitaciones explícitas no resueltas |
| `docs/VISUAL_QUALITY.md` | PSNR/SSIM, ghosting, jitter, MV, desoclusión |
| `docs/PRESENTATION_ARCHITECTURE.md` | BatchPresent, semaphores, acquire/present |
| `docs/FSR4_EXPERIMENTS.md` | Experimentos Wine, DMA-BUF, IPC |
| `docs/NATIVE_FSR4_FEASIBILITY.md` | Por qué FSR 4 nativo Vulkan está bloqueado |
| `docs/PERFORMANCE_OPTIMIZATIONS.md` | Perfilado puente, cuellos de botella |
| `docs/AI_RESEARCH.md` | Investigación corrector neuronal (TEST3 sellado) |

---

## Licencias

| Componente | Licencia | Archivo |
|------------|----------|---------|
| **Código del mod (Java, C++, shaders, scripts, docs)** | MIT | `LICENSE` |
| **AMD FidelityFX SDK** | AMD FidelityFX License | No incluido (descarga separada) |
| **GE-Proton / VKD3D-Proton** | LGPL-2.1+ / MIT | Sistema usuario |
| **Fabric API / Loom / Minecraft** | Sus respectivas licencias | Dependencias Gradle |

**IMPORTANTE**: Las DLLs de AMD (`amd_fidelityfx_loader_dx12.dll`, `amd_fidelityfx_upscaler_dx12.dll`) **NO se redistribuyen**. El usuario debe obtenerlas del SDK oficial y colocarlas en `~/.local/share/fsr4linux/bridge/` via `install-runtime.sh`.

---

## Ramas y Commits de Referencia

| Repositorio | Rama | Commit Base | Estado |
|-------------|------|-------------|--------|
| Minecraft-FSR4-Linux | `experimental/ai-performance` | `8b26e6e` | Investigación activa (neural, particles, disocclusion) |
| Minecraft-FSR4-Beta1 | `perf/frametime` | `8f4d757` | Beta estable, frametime metrics, sin cambios experimentales |

**Cambios sin commit preservados** en `uncommitted-changes-linux.patch` y `uncommitted-changes-beta1.patch`.

---

## Limitaciones Conocidas Críticas

1. **FSR 4.1.1 más lento que nativo** en la escena de prueba (CPU bound en helper Wine + IPC)
2. **FG ×2 AMD no aumenta FPS presentados** en RX 9060 XT con Minecraft vanilla (cuello de botella render thread)
3. **No hay soporte Intel/NVIDIA** (solo AMD RADV con dma-buf)
4. **Requiere GE-Proton + VKD3D-Proton** instalados externamente
5. **DLLs AMD no redistribuibles** — instalación manual obligatoria
6. **Corrector neuronal/desoclusión OFFLINE** (TEST3 sellado, ver `NEURAL_ACCEPTANCE.md`)
7. **Latencia extremo a extremo no medida** (solo estimación software)
8. **Monitor 60 Hz limita FPS presentados visibles** (métricas cuentan `vkQueuePresentKHR`)

Ver `docs/KNOWN_LIMITATIONS.md` para lista completa.

---

## Contribuir / Continuar Desarrollo

```bash
# Rama estable para releases: beta/1.0 (tag: beta-1.0)
# Rama experimental: experimental/ai-performance

# Para continuar investigación neural:
# 1. Regrabar datasets con TICKRATE=1 (scripts/x3-scene.sh)
# 2. Evaluar TEST3 sellado (NEURAL_ACCEPTANCE.md)
# 3. Implementar correspondencias MV + visibilidad reproyectada

# Para optimizar frametime:
# 1. Perfilar con FSR4_GPU_TS=1 + FSR4_NOWAIT=1
# 2. Reducir IPC round-trips (batch requests)
# 3. Explorar async compute overlap real
```

---

## Créditos

- **FSR4-Linux contributors** — Desarrollo principal
- **AMD** — FidelityFX SDK, FSR 4.1.1/3.1.5, FG API 4.0.1
- **GloriousEggroll** — GE-Proton (Wine + VKD3D-Proton)
- **FabricMC** — Fabric Loader, Loom, Fabric API
- **Khronos / Mesa / RADV** — Vulkan, drivers open source

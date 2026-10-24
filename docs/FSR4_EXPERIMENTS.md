# Experimentos FSR 4.1.1

Entorno: Bazzite 44, RADV/Mesa 26.2.4, RX 9060 XT. SDK oficial `FidelityFX-SDK` v2.3.0 (commit `60f4ea81`), asset `FidelityFX-Samples-v2.3.0-prebuilt.zip` (sha256 `f90890b9…8422273`) descargado de GitHub de AMD. DLL usadas (idénticas a `Kits/FidelityFX/signedbin`): `amd_fidelityfx_loader_dx12.dll` sha256 `e2d85aa0…608aa`, `amd_fidelityfx_upscaler_dx12.dll` sha256 `d0dcccc7…9fb46`. Fuentes de los prototipos: `native/backends/fsr4/probe/`. Salidas crudas: `docs/evidence/`.

Los prototipos son ejecutables PE compilados con mingw, ejecutados con `proton run` en un prefijo desechable (`~/.cache/fsr4-linux/pfx*`). No se tocó el sistema anfitrión.

## E1 — ¿Carga el loader oficial y qué proveedores ofrece?
| Runtime | Proveedores listados por `ffxQuery(GET_VERSIONS)` | Observación |
|---|---|---|
| Proton Experimental 11.0-20261001 | 3.1.5, 2.3.4 (**sin 4.1.1**) | El stub `amdxc64.dll` de Proton responde "unknown guid {f714e11a-…}" a la interfaz que pide el DLL 2.3; el proveedor ML no se expone. |
| GE-Proton11-6 | **4.1.1** (listado dos veces con el mismo id `0xf5a5ca1e01001001`), 3.1.5, 2.3.4 | Su `amdxc64` implementa `AMDFSR4FFX_UpdateFfxApiProvider`. |

Resultado: dependiendo de la versión de Proton, 4.1.1 está o no disponible. Es una dependencia de la capa de compatibilidad, no del hardware.

## E2 — ¿Se ejecuta de verdad un dispatch 4.1.1? (GE-Proton11-6)
Escena sintética aliasada 1280×720 → 1920×1080 (Quality 1.5×), jitter Halton del propio SDK (18 fases), vectores de movimiento exactos, 16 frames, `reset` en el primero. Recursos D3D12 reales (color RGBA16F, profundidad R32F, MV RG16F, salida UAV RGBA16F).

| Proveedor | CreateContext | Dispatch | GPU del dispatch (timestamp, frames 4–15) | PSNR vs referencia* |
|---|---|---|---|---|
| 4.1.1 | rc=0 | 16/16 OK | 1.13 ms | 19.85 dB |
| 3.1.5 | rc=0 | 16/16 OK | 0.83 ms | 20.44 dB |
| 2.3.4 | rc=0 | 16/16 OK | 0.76 ms | 20.46 dB |
| (nearest 720p→1080p) | – | – | – | 19.48 dB |

\*Referencia 1080p supersampleada 4×4. **El PSNR de esta escena NO es una métrica de calidad válida**: es un patrón sintético de altísima frecuencia (tablero 24 px, rayas de 7 px, anillos), sin geometría real ni oclusiones; no demuestra que 4.1.1 reconstruya mejor. Solo prueba que la salida es finita, no nula y coherente con la entrada. El signo del vector de movimiento se validó invirtiéndolo: todas las salidas empeoran (4.1.1: 16.28 dB, 3.1.5: 15.17 dB) → la convención usada es la correcta.

### Evidencia de que 4.1.1 es realmente la ruta neuronal (y no un fallback)
1. `strings` sobre `amd_fidelityfx_upscaler_dx12.dll`: cientos de referencias `fsr4_model_v07_fp8_no_scale_{prepass,passN,postpass}` → el modelo ML FP8 va embebido en el DLL oficial.
2. VKD3D-Proton registra `FSR4 resource detected` durante el dispatch 4.1.1 (y `FSR3 resource detected` con 3.1.5) y `vkd3d_supports_minimum_coopmat_caps: … This is required for FSR4`.
3. `ffxQuery(GET_PROVIDER_VERSION)` sobre el contexto activo devuelve `4.1.1` / `0xf5a5ca1e01001001`.
4. Tiempo GPU distinto del de 3.1.5 (+36 %, 1.13 vs 0.83 ms) con la misma carga.
**Limitaciones de la evidencia**: no se ha capturado la ejecución de las capas ML a nivel de ISA (p. ej. con RGP/RADV_DEBUG), ni se ha comparado la imagen con una referencia ML conocida. Queda como paso pendiente (E3).

### Control negativo (fallido como discriminador)
Retirar `amdxcffx64.dll` del prefijo (DLL de AMD del `contrib/` de Proton) **no** cambió nada (mismos resultados). Conclusión: en GE-Proton11-6 con SDK 2.3.0 el 4.1.1 funciona con el modelo embebido en el DLL del SDK; `amdxcffx64.dll` es un mecanismo de actualización del proveedor por el driver. No se asume nada más sobre su función.

## Estado de la ruta nativa Linux/Vulkan
- El SDK 2.3.0 solo contiene `backend/dx12` y `signedbin/*_dx12.dll`; `FFX_API_BACKEND_ID_VK` está reservado en `ffx_api.h` sin cabecera ni DLL de implementación. → **BLOCKED** (ver `FSR4_FEASIBILITY.md`).

## Pendiente
- E3: puente Minecraft (Vulkan nativo) ↔ proceso Wine/VKD3D-Proton (memoria compartida / interop), medir coste.
- E4: comprobar ejecución real de WMMA/FP8 (RGP o `RADV_DEBUG`/contadores).

---
# Sesión 2 — Puente IPC y GPU (Fases A–C)

## E4 — IPC por memoria compartida (CPU) Linux ⇄ Wine con el escalador oficial (Fase A/B)
Código: `native/include/fsr4_ipc.h` (protocolo v1), `native/src/bridge_client.cpp` (lado Linux), `native/backends/fsr4/bridge/fsr4_bridge.cpp` (helper PE), `tests/ipc_test.cpp`. Evidencia: `docs/evidence/IPC_*.txt`, `ipc_compare_nearest_fsr315_fsr411.png`.

- Mapeo `/dev/shm/fsr4-bridge-<pid>` ≡ `Z:\dev\shm\…` en Wine. Handshake por números de secuencia con atomics acquire/release; planes color RGBA16F, depth R32F, MV RG16F, salida RGBA16F con pitch por plano; jitter, escala de MV, deltaTime, preExposure, near/far/fov, flags (reset, depth invertido, HDR, auto-exposición), `frameId`.
- El helper **valida** magic/versión/tamaño del mapeo, dimensiones (0, mayores que el máximo, render>salida), formatos, pitch (< fila, o `pitch*alto` > capacidad), `preExposure>0` y CRC32 de planos (cuando se pide). Rechazo ⇒ `status=kBadRequest` con mensaje; el helper sigue vivo (6 casos negativos + CRC corrompido: todos OK).
- **Qué proveedor corre se comprueba, no se supone**: tras `ffxCreateContext` el helper consulta `GET_PROVIDER_VERSION` y compara con el pedido; si difiere ⇒ `kProviderMismatch`. El cliente lee `activeProvider` del header.
- Resultados con 4.1.1 y 3.1.5 (GE-Proton11-6): secuencia de 64 fotogramas con paneo de 3 px/frame, 64/64 respuestas correctas, `frameId` coherente, CRC de salida coincide byte a byte con la memoria compartida (frames con CRC), 0 píxeles no finitos, 100 % no nulos. PSNR frame 63: 4.1.1 **20.70 dB**, 3.1.5 **20.78 dB**, vecino más cercano 19.48 dB. **Esta escena es patológica (alta frecuencia sintética): el PSNR solo sirve para detectar salidas rotas, no para clasificar calidad.** La comparación visual (`ipc_compare_nearest_fsr315_fsr411.png`) muestra acumulación temporal real en ambos y rayas finas reconstruidas.
- Cambio de resolución Quality 1280×720 → Performance 960×540 → Quality: contexto recreado (≈ 22-28 ms el primer fotograma), salida válida.
- Reset: el flag cambia el resultado del primer fotograma tras un corte de cámara, pero **no** hay evidencia de que mejore la calidad en esta escena (con 4.1.1 el PSNR bajó de 17.86 a 16.35 dB; con 3.1.5 prácticamente igual). Solo se concluye que el flag llega y se procesa.
- Latencia en régimen estable (1080p, Quality, 200 frames, modo CPU/memcpy): ida y vuelta mediana **7.85 ms** (p95 8.63) para 4.1.1 y 6.80 ms para 3.1.5; desglose del helper: subida 1.3 ms, GPU del dispatch 1.15 ms (4.1.1) / 0.59 ms (3.1.5), copia+readback 2.2 ms; sobrecoste puro de IPC (ida y vuelta − tiempo total del helper) ≈ **0.65 ms** (4.1.1) / 0.29 ms (3.1.5).
- Recuperación: kill del helper durante el envío (detectado, sin cuelgue, reinicio ≈ 0.8 s y fotograma correcto); `SIGSTOP` del helper ⇒ `Timeout` a los 1.5 s sin deadlock; **hallazgo y corrección**: los procesos Wine quedan huérfanos al morir el cliente (re-parentados, sesión propia), así que ahora el cliente los localiza por su ruta única en `/proc/*/cmdline` y los mata, y el helper se auto-termina si el cliente deja de renovar su *lease* (verificado: sale 4.1 s después, 0 procesos restantes).

## E3b/E3c/E3d/E3e — Puente GPU (Fase C)
| Prueba | Resultado |
|---|---|
| E3a | Wine (GE) expone `VK_KHR_external_memory_fd`, `VK_EXT_external_memory_dma_buf`, `VK_EXT_external_memory_host` a apps Windows. **Exponer la extensión no implicaba que funcione** (ver abajo). |
| E3b-1 | VKD3D-Proton `CreateSharedHandle` sobre un **buffer** ⇒ handle NULL con `S_OK` y mensaje `Shared texture metadata structure only supports 2D textures`. Solo se pueden compartir **texturas 2D**. Layout `ROW_MAJOR` + `SHARED` ⇒ `E_INVALIDARG`. |
| E3b-2 | Textura 2D `D3D12_TEXTURE_LAYOUT_UNKNOWN` + `ALLOW_SIMULTANEOUS_ACCESS` + `HEAP_FLAG_SHARED` ⇒ handle válido y aparece un **fd `/dmabuf` del tamaño de la textura (16 MiB)** en el proceso Wine. El proceso Linux lo duplica con `pidfd_open`+`pidfd_getfd` (ptrace_scope=0). |
| E3b-3 | Importado en Vulkan nativo (RADV) como `VkImage` (OPTIMAL, usage SRC\|DST\|SAMPLED\|STORAGE\|COLOR, `VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT`, allocación dedicada). Vulkan nativo escribe un patrón lógico 2048² RGBA8 con `vkCmdCopyBufferToImage`; D3D12 bajo Wine lo lee con `CopyTextureRegion` ⇒ **0 / 4 194 304 palabras distintas**. **Interoperabilidad VRAM real demostrada (RGBA8, 2048²).** El primer intento (escribir un buffer lineal en la memoria importada como buffer) dio 4 194 176 palabras erróneas: el layout es *tiled*, hay que importar como imagen con los mismos parámetros. |
| E3c/E3d | `OpenExistingHeapFromAddress` solo acepta la **dirección base** de la región; devuelve `S_OK` pero **no comparte los datos** (CPU→GPU: 1 048 576 palabras erróneas; GPU→CPU: ídem). VKD3D registra `vkd3d_import_host_memory: Failed to import host memory, hr 0x8007000e` y sigue con memoria propia (fallo silencioso). |
| E3e | RADV nativo: `VK_EXT_external_memory_host` **solo importa memoria anónima privada**; `MAP_SHARED` anónimo, memfd, archivo en `/dev/shm` y archivo en disco ⇒ `VK_ERROR_INVALID_EXTERNAL_HANDLE`. Un host-pointer compartido entre dos procesos es **imposible** con amdgpu aquí. Ruta host-pointer: **BLOCKED**. |

Pendiente de E3b: otros formatos (RGBA16F, R32F, RG16F), resolución distinta, sincronización GPU entre procesos (fences compartidas), y medir latencia/costes. Minecraft solo habilita 11 extensiones de dispositivo y **ninguna** de memoria externa (hay que añadirlas con un mixin sobre `VulkanBackend.createDevice`).

## E5 — Ruta GPU sin copias de imagen por CPU (transferMode 2) — Fase C completada
Código: `native/src/gpu_bridge.cpp` (importación Vulkan + auto-verificación), helper `transferMode 2` en `fsr4_bridge.cpp`, `tests/gpu_ipc_test.cpp` (un "juego" Vulkan nativo independiente con su propio `VkDevice` y las extensiones `VK_KHR_external_memory`, `_fd`, `VK_EXT_external_memory_dma_buf`). Evidencia: `docs/evidence/GPU_path_*.txt`.

Cómo funciona (cada paso verificado):
1. El helper crea 4 texturas D3D12 compartidas (color, depth R32F, MV RG16F, salida) `UNKNOWN` + `ALLOW_SIMULTANEOUS_ACCESS` + `HEAP_FLAG_SHARED` + `CreateSharedHandle`.
2. **El fd dma-buf lo guarda `wineserver`** (no el proceso del helper): el cliente toma una instantánea de los fds `/dmabuf` de helper+wineservers del usuario antes y después de la reserva y se queda con los 4 nuevos, en orden de creación; los duplica con `pidfd_open`+`pidfd_getfd`.
3. Los importa como `VkImage` OPTIMAL (`VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT`, dedicados). El layout *tiled* de RADV se reconstruye a partir de los mismos parámetros de imagen; se prueban combinaciones de `usage` hasta que la auto-verificación pasa (en este equipo la primera: `SRC|DST|SAMPLED|STORAGE|COLOR`).
4. **Auto-verificación obligatoria**: el cliente escribe un patrón pseudoaleatorio por plano; el helper lo lee con D3D12 y devuelve el CRC32 de las filas lógicas; deben coincidir en los 4 planos o la ruta no se habilita. (≈ 190 ms por reserva, solo al (re)crear.)
5. Por fotograma: copia GPU `local → compartida` (3 planos), petición por el canal de control, el helper hace `CopyResource` a sus texturas privadas, `ffxDispatch` oficial, `CopyResource` a la salida compartida, y el cliente copia `compartida → local`. La sincronización GPU entre procesos es por ahora **por CPU** (`vkWaitForFences` antes de pedir, espera de respuesta después); no hay semáforos compartidos todavía.

Resultados (1080p, Quality 1280×720, GE-Proton11-6, RADV 26.2.4):
| | 4.1.1 | 3.1.5 |
|---|---|---|
| 64 fotogramas con movimiento, válidos | 64/64 | 64/64 |
| PSNR frame 63 vs referencia (idéntico a la ruta CPU) | 20.70 dB | 20.78 dB |
| Copia de entrada + espera (mediana, escena sin regenerar) | 0.13 ms | 0.13 ms |
| Ida y vuelta IPC + D3D12 (mediana) | **2.23 ms** (helper: 1.48 ms, GPU dispatch 1.14 ms) | **1.65 ms** (helper: 0.91 ms, GPU 0.60 ms) |
| Copia de salida + readback de prueba | 1.31 ms (el readback es solo del test) | 1.31 ms |
| Ruta CPU/memcpy (E4) para comparar | 7.85 ms | 6.80 ms |
- Cambio de resolución por re-importación: 960×540, 1280×720, 1600×900 → 152–216 ms por reserva+verificación, 4 fotogramas válidos en cada una.
- **Pendiente en esta ruta**: sincronización GPU↔GPU sin parar la CPU (fences compartidas), formatos RGBA8 de color/salida y medición en el juego real.
- Bloqueado (con evidencia): host-pointer compartido (E3c–E3e) y heap D3D12 sobre páginas compartidas.

## E4 — fence compartida D3D12 (VKD3D-Proton) ↔ semáforo timeline Vulkan nativo (2026-10-11, sesión 2)
`native/backends/fsr4/probe/e4_fence_{win,native}.cpp`; evidencia `docs/evidence/E4_shared_fence_timeline.txt`.
| Paso | Resultado |
|---|---|
| `CreateFence(FLAG_SHARED)` + `CreateSharedHandle` | OK; aparece un fd nuevo `anon_inode:syncobj_file` (y otro `ntsync`) en el proceso Wine |
| `pidfd_getfd` + `vkImportSemaphoreFdKHR` (OPAQUE_FD, timeline) en RADV nativo | OK (propiedades: features 0x3 = exportable+importable) |
| nativo `vkSignalSemaphore(5)` → D3D12 `GetCompletedValue()` | 5 |
| cola D3D12 `Wait(5)`+`Signal(7)` → nativo `vkGetSemaphoreCounterValue` / `vkWaitSemaphores(7)` | 7 / VK_SUCCESS |
| espera-antes-de-señal entre APIs (cola D3D12 `Wait(20)`, nativo señala 20 después) | la cola D3D12 avanza y señala 21 |
Conclusión: sincronización GPU↔GPU entre Vulkan nativo y D3D12 bajo Wine es posible sin CPU en el camino. Integrado en el puente (kCmdFenceInit/kCmdFenceTest, frames asíncronos con `fenceWait/fenceSignal`). Nota: un fd cerrado y recreado reutiliza su número ⇒ la fence se conserva durante toda la vida del helper (re-importar por diferencia de fds es ambiguo).

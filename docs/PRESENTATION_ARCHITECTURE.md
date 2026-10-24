# Arquitectura de presentación de FG ×3/×4 (sesión 10, 2026-10-13)

Estado: **implementada, medida y validada** (presentación por lotes). Interruptor de retirada: `FSR4_BATCH_PRESENT=0` (vuelve a la ruta anterior, un `submit()` por imagen). Rama `experimental/ai-performance`; `master`/`stable-final-phase` intactos.

## 1. Punto de partida (medido, no supuesto)
FG ×3 propio, 1080p, VSync OFF, escena de `docs/BENCHMARKS.md`: 800 presentados/s (≈266 renderizados). Se instrumentó el hook por segmentos (`fgseg_*` en `bench`): por fotograma renderizado (2 imágenes generadas)

| segmento (CPU, hilo de render) | ms |
|---|---|
| grabar generación + `execute` | 0,026 |
| grabar blit al swapchain | 0,019 |
| **`VulkanCommandEncoder.submit()` (2 llamadas)** | **1,70–1,73** |
| `vkQueuePresentKHR` (2 llamadas) | 0,038–0,040 |
| `vkAcquireNextImageKHR` (2 llamadas) | 0,060 |
| total del hook | 1,84–1,89 de 3,5–3,9 ms de fotograma |

GPU ocupada solo el **60 %** (`gpu_busy_percent`): la GPU esperaba a la CPU y la CPU a la GPU. Causa verificada en el código descompilado de Minecraft 26.3 (`VulkanCommandEncoder.submit()`): tras cada envío espera por CPU (`vkWaitSemaphores`, `awaitSubmitCompletion`) a que termine el envío de hace `MAX_SUBMITS_IN_FLIGHT = 2` envíos, y reinicia la piscina de comandos/uniformes. Con 3 envíos por fotograma (generado 1, generado 2, real) la CPU nunca puede ir más de ~1 fotograma por delante y los fotogramas se serializan.

Experimento de cota (`FSR4_X3_NOPRESENT=1`, solo medición: se calculan los fotogramas ×3 pero no se envían/presentan): **563 fotogramas renderizados/s con la GPU al 98 %**. Es decir, si la presentación fuese gratis el ×3 podría presentar ≈1690/s en lugar de 800: el límite estaba en la estructura de envíos, no en el cómputo.

## 2. ¿Se puede desacoplar la presentación a otro hilo? Conclusión: no aporta, y no es seguro
- `vkQueuePresentKHR` cuesta **0,04 ms** de CPU y `vkAcquireNextImageKHR` 0,06 ms (tabla). Un hilo de presentación solo podría absorber ≈0,1 ms de 1,9 ms: el coste está en `submit()`, que un hilo de presentación no evita porque el envío del blit debe preceder al `present`.
- Vulkan exige sincronización externa sobre `VkQueue`. Minecraft envía y presenta por la **misma cola gráfica** (`VulkanGpuSurface.presentQueue == graphicsQueue`) sin ningún cerrojo; un segundo hilo necesitaría un cerrojo de cola común inyectado por mixin en `VulkanQueue.Submission.close()` y en `present()`. Riesgo de bloqueo/carrera alto y beneficio medido ≈0. **No se implementó** (se documenta como descartado con datos).
- Lo que sí se hizo: reducir los envíos por fotograma a **uno** (como el juego nativo) y emitir los `vkQueuePresentKHR` en orden justo después (todo en el hilo de render, sin concurrencia).

## 3. Diseño implementado (`BatchPresent`, `VulkanGpuSurfaceMixin`, `MinecraftMixin`)
1. En el hook de presentación (antes del blit del fotograma real) se graba, para cada fotograma generado `i` (1..N−1): el cómputo ×N (`recordGenerateN`, escribe en su **propio** destino `genTarget`/`genTarget2`/`genTarget3`) y, a continuación, **se adquiere una imagen del swapchain con semáforos de adquisición propios** (anillo de 16; el anillo de Minecraft solo tiene 2) y se graba su blit en la **misma entrega** pendiente del codificador (`VulkanGpuSurface.blitFromTexture` reutilizado: espera el semáforo, copia con la inversión vertical de Minecraft, señala `presentSemaphores[imagen]`).
2. Minecraft hace su blit del fotograma real y su **único** `submit()`.
3. En lugar de `windowSurface.present()` (`@Redirect` en `Minecraft.renderFrame`) se presentan **en orden temporal**: generado 1, …, generado N−1 y por último el real. Mismo orden que antes, **sin duplicar imágenes**: si no hay imagen disponible en 50 ms (`ACQUIRE_TIMEOUT`) o el swapchain está obsoleto, simplemente se presentan menos imágenes ese fotograma.
4. Swapchain: `minImageCount` pasa a 10 con `IMMEDIATE`/`MAILBOX` (sin cola de vblank no añade latencia); con `FIFO` (VSync) se mantiene el de Minecraft (3) y se usa la ruta anterior, porque ahí la pantalla —no la GPU— limita el ritmo y cada imagen extra costaría un vblank de latencia (medido, §5).
5. Cambio de resolución / pantalla completa / recreación del swapchain: Minecraft reconfigura al inicio del fotograma con la superficie **no** adquirida, cuando no hay lote pendiente; los pases ×N se re-crean en el siguiente límite. La validación Vulkan encontró dos copias fuera de rango en esos fotogramas de transición (origen 1280×720, destino 1920×1080): corregidas con `fgSizeMatches` (§4). Una imagen cuyo `acquire` falla se descarta sin grabar nada.
6. Cierre del juego: `BatchPresent.release` destruye los semáforos tras `waitForGpuIdle`. Se corrigió además una **condición de carrera preexistente** `BridgeClient::stop()` ⟷ `start()` (el hilo que arranca el helper seguía leyendo la memoria compartida ya desmapeada → SIGSEGV al salir mientras el helper arrancaba): `stop()` ahora marca `abortStart_` y espera al cerrojo.
7. Sin hilos nuevos, sin colas nuevas, sin esperas añadidas: no hay posibilidad de interbloqueo nuevo (la única espera añadida es el `acquire` con tiempo máximo de 50 ms).

## 4. Validación (capa Khronos + validación de sincronización)
`scripts/validate-vk.sh {fg3|fg4|fsr_fg3}`: la capa `VK_LAYER_KHRONOS_validation` 1.4.328 (compilada localmente sin root en `~/.cache/fsr4-linux/vvl`) con `VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`, sesión real con vuelo, giro, **dos conmutaciones de pantalla completa**:
- ×3, ×4 y FSR 4.1.1 + ×3: **0 hallazgos de sincronización o uso en nuestro código** (sí ≈90 000 `WRITE_AFTER_WRITE` en `vkCmdWriteBufferMarker2AMD`, marcadores de diagnóstico del propio Minecraft, filtrados en el informe).
- Encontrado y **corregido**: `VUID-vkCmdCopyImage-srcOffset-00144` (copia de 1920 px desde una imagen de 1280 px en los fotogramas de transición de tamaño).
- Preexistente, **no** corregido (documentado): (a) `VUID-VkImageCreateInfo-pNext-00990` / `VkImportMemoryWin32HandleInfoKHR-09861`: las imágenes compartidas con el helper usan `DMA_BUF` con tiling `OPTIMAL`, que RADV acepta pero la consulta de formato externa no declara compatible (sin esos VUID filtrados la capa devuelve `VK_ERROR_VALIDATION_FAILED` y la ruta no arranca; con RADV real funciona); (b) fugas al salir (`vkDestroyDevice`): pipelines/layouts/samplers/pools de los tres pases nativos que no se liberan al cerrar el proceso.
La variable de entorno de las capas no se hereda al helper de Wine (`unsetenv` en el `fork`).

## 5. Resultados (1080p, escena de BENCHMARKS.md, 3 ventanas de 200 ticks por sesión)
Dos sesiones de juego independientes (A, B), 3 ventanas de 200 ticks cada una; entre paréntesis las 3 ventanas. CPU = núcleos usados por el proceso del juego (jiffies de /proc cada 0,2 s). "Ruta anterior" = `FSR4_BATCH_PRESENT=0`. FG GPU ms de AMD = dispatch medido por el helper (el ×3/×4 propio no usa el helper; su coste GPU está en `docs/FG_X3.md`). Datos: `benchmarks/s10/suite_*`.

### sesión A
| modo | renderizados FPS | generados FPS | **presentados FPS** | GPU busy % | VRAM MB | CPU núcleos (juego) | p99 ms | 1% low FPS | FG GPU ms | lat. hook CPU ms |
|---|---|---|---|---|---|---|---|---|---|---|
| Nativo | 1209.3 (1207.5/1220.8/1199.5) | 0.0 | **1209.3 (1207.5/1220.8/1199.5)** | 96 | 2160 | 1.30 | 1.85 | 412 | 0.00 | 0.00 |
| FSR 4.1.1 Quality | 536.9 (534.6/540.2/535.8) | 0.0 | **536.9 (534.6/540.2/535.8)** | 94 | 2295 | 0.83 | 3.05 | 260 | 0.00 | 0.00 |
| FG x2 AMD | 232.2 (212.7/233.1/250.8) | 232.2 | **464.5 (425.5/466.2/501.7)** | 68 | 2291 | 0.73 | 6.29 | 129 | 1.38 | 1.52 |
| FG x3 (lotes) | 533.0 (533.6/533.4/531.9) | 1065.9 | **1598.8 (1600.7/1600.1/1595.7)** | 99 | 2227 | 0.85 | 2.93 | 274 | 0.00 | 0.10 |
| FG x3 (ruta anterior) | 303.5 (287.8/309.8/313.0) | 607.1 | **910.6 (863.3/929.5/938.9)** | 67 | 2160 | 0.83 | 5.08 | 158 | 0.00 | 1.87 |
| FG x4 (lotes) | 465.5 (462.3/467.2/467.0) | 1396.5 | **1862.0 (1849.1/1868.9/1868.0)** | 99 | 2219 | 0.88 | 3.34 | 242 | 0.00 | 0.14 |
| FSR+FG x2 AMD | 208.9 (203.4/211.9/211.4) | 208.9 | **417.8 (406.8/423.8/422.8)** | 70 | 2378 | 0.79 | 7.59 | 117 | 1.22 | 2.21 |
| FSR+FG x3 (lotes) | 415.9 (421.9/416.3/409.5) | 831.7 | **1247.6 (1265.6/1248.9/1228.4)** | 96 | 2325 | 0.78 | 3.63 | 218 | 0.00 | 0.10 |
| FSR+FG x3 (anterior) | 294.2 (280.4/295.6/306.6) | 588.4 | **882.6 (841.3/886.7/919.7)** | 72 | 2266 | 0.75 | 5.32 | 158 | 0.00 | 2.21 |
| FSR+FG x4 (lotes) | 379.7 (379.9/382.2/377.0) | 1139.1 | **1518.8 (1519.5/1528.7/1508.1)** | 96 | 2325 | 0.87 | 3.97 | 195 | 0.00 | 0.13 |
### sesión B
| modo | renderizados FPS | generados FPS | **presentados FPS** | GPU busy % | VRAM MB | CPU núcleos (juego) | p99 ms | 1% low FPS | FG GPU ms | lat. hook CPU ms |
|---|---|---|---|---|---|---|---|---|---|---|
| Nativo | 1193.1 (1183.8/1201.5/1194.1) | 0.0 | **1193.1 (1183.8/1201.5/1194.1)** | 96 | 2103 | 1.31 | 1.83 | 414 | 0.00 | 0.00 |
| FSR 4.1.1 Quality | 525.3 (518.4/527.6/530.0) | 0.0 | **525.3 (518.4/527.6/530.0)** | 95 | 2271 | 0.83 | 3.05 | 238 | 0.00 | 0.00 |
| FG x2 AMD | 233.4 (217.7/232.6/250.0) | 233.4 | **466.9 (435.3/465.3/500.0)** | 68 | 2274 | 0.75 | 6.31 | 135 | 1.38 | 1.50 |
| FG x3 (lotes) | 541.6 (541.6/543.9/539.4) | 1083.3 | **1625.0 (1624.9/1631.7/1618.3)** | 99 | 2210 | 0.89 | 3.03 | 270 | 0.00 | 0.10 |
| FG x3 (ruta anterior) | 301.7 (283.5/302.7/318.9) | 603.4 | **905.1 (850.5/908.2/956.7)** | 67 | 2151 | 0.82 | 5.00 | 160 | 0.00 | 1.88 |
| FG x4 (lotes) | 446.0 (440.7/449.1/448.3) | 1338.2 | **1784.2 (1763.0/1796.3/1793.3)** | 99 | 2210 | 0.94 | 3.38 | 232 | 0.00 | 0.14 |
| FSR+FG x2 AMD | 206.1 (199.9/207.2/211.2) | 206.1 | **412.2 (399.8/414.4/422.5)** | 71 | 2377 | 0.66 | 7.31 | 117 | 1.25 | 2.23 |
| FSR+FG x3 (lotes) | 405.3 (407.1/408.2/400.7) | 810.7 | **1216.0 (1221.2/1224.7/1202.2)** | 96 | 2325 | 0.82 | 3.84 | 219 | 0.00 | 0.09 |
| FSR+FG x3 (anterior) | 262.2 (241.5/266.5/278.7) | 524.4 | **786.7 (724.5/799.5/836.0)** | 70 | 2264 | 0.91 | 6.13 | 129 | 0.00 | 2.34 |
| FSR+FG x4 (lotes) | 372.4 (374.0/374.8/368.4) | 1117.2 | **1489.6 (1495.9/1499.1/1473.8)** | 97 | 2323 | 0.80 | 3.90 | 199 | 0.00 | 0.13 |


### VSync ON a 100 Hz (pantalla real) con sonda `VK_KHR_present_wait`
`FSR4_PRESENT_PROBE=1` (diagnóstico, solo medición): cada `vkQueuePresentKHR` lleva un `presentId` y otro hilo espera `vkWaitForPresentKHR`; el retorno = la imagen fue presentada por el compositor. Con **VSync ON** esto mide lo que realmente se muestra (intervalo medio 10,00 ms ±0,05 = 100,0 imágenes/s, coincide con el monitor):

| modo (FIFO, 100 Hz) | renderizados | presentados = observados | latencia de cola present→visible media / p99 ms |
|---|---|---|---|
| nativo | 100 | 100,0 | 62,5 / 63,2 |
| FG ×2 AMD | 50 | 100,0 | 62,3 / 64,8 |
| FG ×3 (ruta por imagen, la que se usa en FIFO) | 33,3 | 100,0 | **33,4 / 34,5** |
| FG ×3 (por lotes, 6 imágenes) — descartado en FIFO | 33,3 | 100,0 | 54,5 / 65,0 |
| FG ×4 (ruta por imagen) | 25,0 | 100,0 | 33,7 / 34,8 |
Es decir: en VSync FG no sube los FPS mostrados (siguen siendo 100), solo cambia el ritmo de renderizado (100 → 50/33/25) y la latencia; la ruta por lotes añadiría ≈21 ms de cola (más imágenes en vuelo), por eso en FIFO se mantiene la ruta anterior. **El monitor secundario (60 Hz, HDMI-A-2) existe y la ventana puede abrirse en él**: `scripts/probe-run.sh` repite las medidas VSync hasta que el intervalo medido es el de 100 Hz.

### VSync OFF
Con `IMMEDIATE` la sonda devuelve "presentado" a ritmo de envío (obs ≈ presentados, 1500–1740/s con ráfagas: 50–75 % de intervalos <1 ms) — **no** es evidencia de imágenes visibles: el compositor (KDE/Wayland, ventana normal) compone a 100 Hz y no se midió con captura de pantalla/hardware. **No se afirma ningún "FPS observable" por encima de 100 Hz.**

## 6. Limitaciones
- Las cifras de "presentados" cuentan `vkQueuePresentKHR`; el monitor muestra ≤100 imágenes/s (VSync) o flips con desgarro (VSync OFF, sin medir).
- La latencia extremo a extremo (entrada→fotón) no se midió (sin hardware); solo la cola de presentación con la sonda en VSync.
- ×4 con VSync a 100 Hz renderiza a 25 FPS: no tiene sentido práctico (el múltiplo debe elegirse según FPS renderizados/Hz).
- Los fotogramas generados salen del mismo `x3`/`x4` (calidad vs. verdad en `docs/FG_X3.md` y `docs/VISUAL_QUALITY.md`).
- La capa de validación y la sonda no se distribuyen ni se activan por defecto.

## 7. Reproducir
`scripts/build-native.sh && scripts/install-runtime.sh`; mod: `gradlew build`. Modos: `scripts/bench-mode.sh OUT.csv {native|fsr|fg2|fg3|fg4|fsr_fg2|fsr_fg3|fsr_fg4}` (+`FSR4_BATCH_PRESENT=0` para la ruta anterior, `VSYNC=true`); suite completa: `scripts/bench-suite.sh` y `scripts/suite-report.py`; sonda: `scripts/probe-run.sh MODO ETIQUETA [VSYNC=true]`; validación: `scripts/validate-vk.sh fg3`.

# Perfilado y optimizaciones (sesión 7, 2026-10-12)

Datos crudos: `benchmarks/s7/*.csv` (+ `.testlog`, `.gpu`). 1920x1080 de salida, render distance 16, VSync OFF, máquina en reposo (`scripts/bench-run.sh`), 3 repeticiones de 200 ticks. Marcas de tiempo GPU: `FSR4_GPU_TS=1` (nuevo; `PrepPass::gpuTimes`, `fgGpuTimes`).

## 1. Línea base repetida (código de la sesión 6, sin cambios de comportamiento)
| Modo | FPS (r1/r2/r3) | frame ms | p99 ms | 1 % low FPS |
|---|---|---|---|---|
| Nativo | 1134 / 1149 / 1143 | 0.87–0.88 | 1.94–2.01 | 347–387 |
| FSR 4.1.1 Quality (720p interno) | 537 / 543 / 538 | 1.84–1.86 | 3.08–3.14 | 240–259 |
Coincide con la sesión 5 (≈1100 / ≈545): sin deriva.

## 2. Perfil GPU del fotograma con FSR 4.1.1 (marcas de tiempo dentro de la cola de Minecraft, 3 repeticiones)
| Tramo | ms | Qué es |
|---|---|---|
| prep | 0.058 | depth + vectores de movimiento + copia del color a los planos compartidos |
| cadena de upscale | 1.05 (1.046–1.057) | señal → helper (IPC) → dispatch DX12 de AMD (**0.93 ms**, medido por el helper) → fence → Minecraft |
| copia de salida | 0.016 | |
| resto de la cola | 0.76–0.79 | mundo a 720p + HUD + presentación (+ huecos) |
| **suma** | ≈1.9 | = frame time medido (1.88–1.92 con marcas activas) |
Lectura: la cola GPU está serializada de extremo a extremo. El puente (prep + traspaso 0.12 ms + copia) cuesta ≈0.19 ms (≈10 %); lo demás es el dispatch de AMD (≈49 %) y el trabajo propio de Minecraft (≈41 %). Minecraft apenas depende de la resolución interna (540p 568 FPS vs 720p 540 FPS en BENCHMARKS.md): su coste GPU es ≈0.8 ms casi fijo.
Coste CPU del puente en el hilo de render: 0.31–0.46 ms (`gpuwait_ms` 0.23–0.31 es el `vkQueueSubmit` del vaciado intermedio), pero no está en el camino crítico: la GPU es el límite.

## 3. Candidatas evaluadas (cada una medida, 3 repeticiones, mismas condiciones)
| Candidata | FPS (r1/r2/r3) | Resultado |
|---|---|---|
| Línea base FSR 4.1.1 | 520 / 530 / 531 (con marcas) | – |
| Helper en cola D3D12 **COMPUTE** (`FSR4_QTYPE=compute`) | 529 / 539 / 538 | **sin diferencia** (VKD3D no obtiene concurrencia real; cadena 1.03–1.04 ms). Se deja como interruptor experimental, desactivado. |
| **Sin esperar la salida del upscaler** (`FSR4_NOWAIT=1`, cota superior del solapamiento; la imagen mostrada es incorrecta, SOLO experimento de tiempo) | 556 / 565 / 555 | **+3–5 % como máximo**: la GPU sigue saturada porque el dispatch de AMD ocupa todo el chip; el trabajo restante de Minecraft no se solapa. |
| Ambas | 561 / 573 / 569 | +6–8 % máx. (cota superior, no alcanzable sin doble búfer de salida y +1 fotograma de latencia) |
Decisión: **ninguna se integra por defecto**. El solapamiento asíncrono (pipeline de un fotograma de retraso con doble búfer del plano de salida) tendría un techo de +3–8 % medido, con coste de latencia y riesgo de sincronización: no compensa. El resto del puente (≈0.19 ms) no tiene candidatas de >5 %. **No hay optimización que mejore FSR 4.1.1 de forma real y estable en esta arquitectura**; el siguiente cuello de botella es el dispatch de AMD FSR 4.1.1 (0.93 ms @1080p, no controlable desde el mod sin bajar la resolución de salida, lo que está prohibido) y el trabajo GPU fijo de Minecraft (≈0.8 ms).

## 4. Frame Generation: de dónde salen los 1.3–2.0 ms (solo FG, 1080p, 3 repeticiones)
| Medida | r1 / r2 / r3 |
|---|---|
| FPS renderizados / generados / presentados | 231–244 / igual / 462–488 (otra tanda: 237–259 / 474–517) |
| Tiempo de fotograma renderizado | 4.33 / 4.26 / 4.10 ms |
| Dispatch FG de AMD (GPU, medido por el helper) | 1.48 / 1.47 / 1.43 ms |
| Cadena FG en la cola GPU (copia entrada → helper → FG → copia salida) | 1.64 / 1.63 / 1.58 ms (entrada 0.023, salida 0.025) |
| **Hueco de la cola GPU entre cadenas** | **2.65 / 2.58 / 2.48 ms** |
| IPC `fgPost` (CPU) | 0.006–0.010 ms |
| Hook de FG en CPU (total) | 1.31–1.34 ms: entrada 0.03, IPC 0.01, copia salida 0.01, **`submit()` 1.14–1.16 ms**, present 0.08, acquire 0.03 |
Diagnóstico (medido: IPC/entrada/copias ≈0.02–0.05 ms; `submit()` 1.15 ms; huecos GPU 2.5–2.6 ms; INFERIDO, no probado directamente: el mecanismo de bloqueo): el coste NO es IPC ni sincronización explícita (≤0.02 ms). El dispatch de FG (1.45 ms) más 0.17 ms de traspaso forma una cadena que se serializa con el resto del fotograma. Además (verificado en el bytecode) `VulkanCommandEncoder.submit()` de Minecraft espera en CPU (`awaitSubmitCompletion`, `vkWaitSemaphores`) a que termine el envío de hace `MAX_SUBMITS_IN_FLIGHT = 2` envíos; con 3 envíos por fotograma (copia de entrada, generado, real) la hipótesis coherente con los tiempos es que esa espera cae sobre la cadena de FG del fotograma anterior: **1.15 ms de CPU bloqueada** dentro de `submit()`, y la GPU queda ociosa mientras la CPU renderiza el siguiente fotograma (hueco 2.6 ms). Con FSR + FG el hueco sube a 3.3–3.5 ms (3 envíos más el de volcado del mundo).
Límite teórico (aritmética sobre lo medido, no medido): sin esos huecos el fotograma GPU sería ≈ mundo 0.9 + cadena FG 1.6 + blits ≈ 2.7 ms ⇒ ≈370 renderizados / ≈740 presentados, aún muy por debajo de los 1140 FPS nativos. Reducir envíos por fotograma exige reordenar la entrega del generado (el generado depende de la entrada del fotograma N y precede al real N: la dependencia es inherente al diseño de AMD FG); no se intentó porque cambiaría latencia y orden de presentación y no puede superar al nativo. **El modelo oficial de AMD FG no se tocó.**

# Benchmarks — resultados REALES dentro de Minecraft 26.3

## Sesión 9: FG ×3 propio vs FG ×2 AMD — 1080p, render distance 16, Fancy, VSync OFF, 3 repeticiones de 200 ticks, máquina en reposo
Renderizados / generados / presentados son magnitudes separadas (`docs/evidence/session9/bench_1080p.md`; CSV en bruto en `~/Escritorio/FSR4-evidence-session9/bench`).
| Modo | Renderizados | Generados | Presentados | Latencia añadida CPU ms | p99 ms | 1% low FPS |
|---|---|---|---|---|---|---|
| Nativo 1080p | 1180,8 (1177/1183/1182) | 0 | 1180,8 | 0,14 | 1,93 | 398 |
| Solo FG ×2 (AMD) | 218,0 (210/214/230) | 218,0 | 436,1 | 3,13 | 6,34 | 130 |
| **Solo FG ×3 (propio)** | 288,1 (276/288/301) | 576,3 | **864,4** (828/863/902) | 2,22 | 5,13 | 148 |
| FSR 4.1.1 Quality | 539,8 (530/551/539) | 0 | 539,8 | 0,72 | 3,01 | 271 |
| FSR 4.1.1 + FG ×2 (AMD) | 200,6 (192/202/208) | 200,6 | 401,3 | 3,60 | 7,05 | 115 |
| **FSR 4.1.1 + FG ×3 (propio)** | 275,5 (263/279/285) | 551,0 | **826,5** (788/837/854) | 2,44 | 5,37 | 153 |
- FSR 4.1.1 + ×3 presenta 2,06× las imágenes de FSR 4.1.1 + ×2 AMD y +53 % sobre FSR 4.1.1 sin FG; con latencia CPU de hook menor. En esta escena limitada por CPU el nativo (1181) sigue presentando más que cualquier modo con FG: **no se alcanzan 2000 FPS**.
- **Latencia estimada (modelo, no medida con hardware)**: el fotograma real se presenta una vez generados los intermedios, así que añade ≈ un intervalo de render + el cómputo de generación: FSR 4.1.1 + ×3 ≈ 1000/275,5 + 0,7 = **4,3 ms** (FSR sin FG: el fotograma dura 1,85 ms); FSR 4.1.1 + ×2 AMD ≈ 1000/200,6 + 1,3 = 6,3 ms.
### Perfil GPU de FG ×3 (par de fotogramas, `FSR4_GPU_TS=1`; `docs/evidence/session9/x3_profile_1080p.txt`)
- En juego a 1080p con cámara fija: **0,707 ms** el par (0,707/0,705/0,707; antes de la sesión 9: 0,68), 58 MiB. Con mediciones por pase (reproducción 1080p): vaciado 0,04 + proyección de profundidad 0,05–0,06 + registro 0,035–0,045 + resolución 0,10 (cámara fija) / 0,20 (cámara móvil) ms **por fotograma**: total del par **0,52 ms (fija) / 0,84 ms (móvil)**. La resolución domina.
- **Optimización realizada**: la búsqueda de huecos recorría una ventana 17x17 por píxel hueco; ahora crece por anillos y se detiene dos–cuatro anillos tras el primer vecino válido. En reproducciones de 720p con muchos huecos: **0,78 → 0,44 ms** el par (desoclusión), calidad igual (34,92 vs 34,97 dB). **No mejoró la medición en juego con cámara fija** (0,68 → 0,71, ruido): ahí casi no hay huecos.
- Oportunidades identificadas y NO implementadas (ganancia estimada ≤15–20 %, riesgo > beneficio): fusionar los dos fotogramas en una sola pasada de proyección compartiendo lecturas; sustituir las dos pasadas de ganador (profundidad + id) por un `atomicMax` de 64 bits (requiere activar `shaderBufferInt64Atomics` en el dispositivo de Minecraft vía mixin). Perfil en reproducción: la GPU en ráfagas cortas va a relojes bajos, las cifras absolutas de la reproducción no son comparables con las del juego.


## Sesión 8: FG ×3 propio (experimental) frente a FG ×2 AMD — 1080p, render distance 16, Fancy, VSync OFF, 3 repeticiones de 200 ticks, máquina en reposo
Renderizados / generados / presentados son magnitudes separadas. Datos en bruto: `~/Escritorio/FSR4-evidence-session8/bench/*.csv` (fuera del repo); tabla y análisis en `docs/FG_X3.md`.
| Modo | Renderizados | Generados | Presentados |
|---|---|---|---|
| Nativo | 1163,5 (1165/1160/1165) | 0 | 1163,5 |
| Solo FG ×2 (AMD) | 228 (221/227/236) | 228 | 456 |
| Solo FG ×3 (propio) | 269,2 (260/263/284) | 538,4 | 807,6 (781/790/852) |
| FSR 4.1.1 Quality | 537,2 (527/546/539) | 0 | 537,2 |
| FSR 4.1.1 + FG ×2 AMD | 199,6 (192/196/211) | 199,6 | 399,2 |
| FSR 4.1.1 + FG ×3 propio | 262,1 (246/261/279) | 524,1 | 786,2 (737/784/838) |
GPU FG ×3: 0,68 ms para los dos fotogramas generados (3 mediciones: 0,681/0,680/0,678), 58 MiB. La barrera añadida al pase de preparación no cuesta nada medible (solo FG ×2: 221/227/236 con barrera vs 217/222/229 sin ella). Entre sesiones el FG-solo ×2 varía (251 en la sesión 4, 228 hoy). **Ningún modo con FG supera al nativo en esta escena limitada por CPU; no hay 2000 FPS.**

Dos tandas: **(A) 2026-10-11 sesión 1** (puente original, sección final "Histórico") y **(B) sesión 2 — tras la optimización del camino crítico**. Los resultados sintéticos (tests nativos) están separados al final.

## Metodología (B)
`scripts/bench-run.sh` + `TestHarness` (FSR4_SCRIPT): mundo `FSRTest` (semilla fija, creativo, vuelo, cámara fija en 300,140,300 yaw 30 pitch 20, `time set noon`), VSync OFF, maxFps 260, `inactivityFpsLimit=minimized`, RX 9060 XT / RADV 26.2.4 / GE-Proton11-6. 200 ticks (~10 s) por medición, **3 repeticiones por modo** (consecutivas dentro de la misma sesión, sin reiniciar), máquina en reposo (el script aborta o espera si hay `stress-ng`/`clpeak`/`cpu_bench`, GPU>15 % o load>=3). GPU busy % y VRAM se muestrean cada 200 ms de `/sys/class/drm/card1/device` y se promedian dentro de cada ventana (`scripts/bench-report.py`). Datos crudos: `docs/evidence/opt/final_*.csv|md`, `benchmarks/opt/`.
Escena ESTÁTICA con un solo punto de vista. "Coste en hilo de render" = tiempo CPU de la llamada de FSR en el hilo de render (no es latencia). La columna *modo* del CSV es la autoridad (en `final_4k_heavy` los nombres de fila `f411q/b/p` están desplazados por un error de pulsaciones F9: ver la columna modo/interno).

## B1. CPU-bound — 1080p, render distance 16, preset Fancy (escenario del monitor del usuario)
| Modo | Interno | FPS medio (3 reps) | 1% low | frametime ms | dispatch AMD GPU ms | coste CPU hilo render ms |
|---|---|---|---|---|---|---|
| Nativo | 1920x1080 | **1119** (1112/1124/1121) | 330–373 | 0.89 | – | – |
| FSR 3.1.5 Quality | 1280x720 | 671 (668/677/668) | 286–297 | 1.49 | 0.49 | 0.37 |
| FSR 4.1.1 Quality | 1280x720 | 540 (541/539/541) | 266–286 | 1.85 | 0.92 | 0.46 |
| FSR 4.1.1 Balanced | 1129x635 | 556 | 271–297 | 1.80 | 0.93 | 0.54 |
| FSR 4.1.1 Performance | 960x540 | 568 (560/572/573) | 226–297 | 1.76 | 0.92 | 0.48 |

**FSR NO aumenta los FPS aquí**: el fotograma nativo (0.89 ms) es más barato que el dispatch de FSR 4.1.1 (0.92 ms) por sí solo. La resolución interna sí cambia (columna "interno"), pero el mundo ya cuesta casi nada.

## B2. GPU-bound — 3840x2160 (salida), render distance 32, preset Fabulous + improvedTransparency
| Modo | Interno | FPS medio (3 reps) | 1% low | frametime ms | dispatch AMD GPU ms | VRAM max MB |
|---|---|---|---|---|---|---|
| Nativo | 3840x2160 | **289.6** (290.0/290.2/288.5) | 136–172 | 3.45 | – | 2654 |
| FSR 3.1.5 Quality | 2560x1440 | 216.2 | 97–143 | 4.63 | 1.97 | 3390 |
| FSR 3.1.5 Performance | 1920x1080 | 265.8 | 159–179 | 3.76 | 1.62 | 3028 |
| **FSR 3.1.5 Ultra Performance** | 1280x720 | **312.7 (+8.0 % vs nativo)** | 171–203 | **3.20** | 1.36 | 2944 |
| FSR 4.1.1 Balanced | 2259x1271 | 150.7 | 111–119 | 6.64 | 4.18 | 3141 |
| FSR 4.1.1 Performance | 1920x1080 | 158.7 | 119–125 | 6.30 | 4.10 | 3124 |
| FSR 4.1.1 Ultra Performance | 1280x720 | 167.5–171.0 | 125–132 | 5.85–5.97 | 4.03–4.07 | 3096 |
(FSR 4.1.1 Quality a 4K, medido antes de la última optimización "direct": 132 FPS.) Escenarios intermedios (4K, render distance 16 y 32, preset Fancy: nativo 507–525 / 400–412 FPS; FSR 3.1.5 Q 264/226; FSR 4.1.1 Q 155/143) en `docs/evidence/opt/scn_*.csv`.

## Conclusión sobre cuándo FSR aumenta los FPS (con datos)
- **FSR 4.1.1: en ningún escenario medido supera al nativo.** Su dispatch cuesta ≈0.93 ms a 1080p de salida y ≈4.1 ms a 4K (coste ∝ píxeles de SALIDA, ≈0.5 ms/Mpíxel, independiente del modo de calidad), más que el fotograma nativo completo del Minecraft vanilla en esta GPU (0.9 ms a 1080p, 2–3.5 ms a 4K). Para ganar haría falta un fotograma nativo de ≳5.5 ms a 4K (≳2 ms a 1080p) por *coste por píxel* mucho mayor que el vanilla (p.ej. un renderizador con shaders pesados, que Minecraft Vulkan 26.3 vanilla no tiene).
- **FSR 3.1.5: solo gana en 4K con el preset más pesado y el modo Ultra Performance (+8 %)**, que es el modo de peor calidad de imagen (escala 3x). A 1080p y en Quality/Balanced/Performance a 4K es más lento que nativo.
- Modelo de coste medido: `T_fsr ≈ T_mundo(res_interna) + T_dispatch(res_salida) + ~0.4–0.5 ms CPU/sync`. FSR compensa solo si `T_mundo(nativo) − T_mundo(interna) > T_dispatch + overhead`.

## Optimización del puente (Fase 1–2): FSR 4.1.1 Quality, 1080p, escena estática, 3 reps cada una
| Versión | FPS medio | frametime ms | camino crítico medido (ms) |
|---|---|---|---|
| Base (sesión 1; `FSR4_LEGACY_WAIT=1`, `FSR4_SYNC=cpu`, `FSR4_NO_DIRECT=1`) | 270 (265/272/277) | 3.69 | GPU-wait CPU 0.79 + IPC 1.76 (de ello fence del helper 1.19 [dispatch GPU 1.03], CPU helper 0.09, **latencia de sondeo/despertar ≈0.4**) |
| + busy-poll acotado en helper y cliente (commit f41134f) | 331 (317/336/341) | 3.02 | GPU-wait 0.78 + IPC 1.24 (latencia de sondeo ≈0) |
| + sincronización GPU↔GPU con fence compartida D3D12↔timeline Vulkan (commit 5580bf3) | 517 (509/517/521) | 1.94 | render thread no espera: submit 0.4 + IPC 0.07 |
| + dispatch directo sobre las texturas compartidas (sin copias en el helper) — **versión final** | 535 (A/B intercalado, 2 pares: 510→535, +5 %) | 1.87 | idem, −4 copias GPU por fotograma |
Mejora acumulada: **+98 % FPS** (270→535) en 4.1.1 Quality 1080p. Camino crítico final = trabajo real de GPU en serie (mundo ≈0.5 + prep + dispatch 0.92 + HUD): ya no hay burbujas CPU↔GPU. No se sumaron tiempos GPU solapados; `gpu_dispatch` son timestamps D3D12 solo alrededor de ffxDispatch.
Latencia: el resultado de cada fotograma se usa en ese mismo fotograma (sin retardo de un frame ni reutilización del resultado N-1); la fence solo reordena trabajo GPU.

## Sintéticos (no son Minecraft)
Tests nativos (`tests/ipc_test`, `tests/gpu_ipc_test`, ver docs/evidence): p. ej. gpu_ipc_test 1080p Quality: ~2.57 ms/frame serializado (ruta CPU-wait antigua).

## Histórico — sesión 1 (puente original, 1080p, escena estática, una repetición)
| Modo | FPS | 1% low |
|---|---|---|
| Nativo | 1099 | 442 |
| FSR 3.1.5 Quality | 279 | 158 |
| FSR 3.1.5 Balanced | 288 | 155 |
| FSR 3.1.5 Performance | 293 | 173 |
| FSR 4.1.1 Quality | 264 | 142 |
| FSR 4.1.1 Balanced | 268 | 163 |
| FSR 4.1.1 Performance | 269 | 148 |

## Trampas encontradas (evitar falsos resultados)
- `inactivityFpsLimit="afk"` (por defecto) limita a EXACTAMENTE 30 FPS tras 60 s sin entrada.
- Cargas externas (`stress-ng`, `clpeak`, `cpu_bench.sh`) degradan los FPS: `scripts/bench-run.sh` las detecta (`WAIT_IDLE=1` espera).
- El preset gráfico `fancy` sobrescribe `renderDistance` al cargar: hay que usar `graphicsPreset:"custom"` o `fabulous` (el script ya lo hace; también restablece el perfil entre ejecuciones).
- Con VSync ON y el monitor a 100 Hz el límite sería 100 FPS.

# Frame Generation (sesión 4) — FPS renderizados, generados y presentados POR SEPARADO

Metodología: igual que arriba (`scripts/bench-run.sh`, 3 repeticiones consecutivas por modo, máquina en reposo, VSync OFF salvo indicación) + contadores propios: **renderizados** = fotogramas del juego, **generados** = salida del proveedor AMD FG 4.0.1 entregada al swapchain, **presentados** = `vkQueuePresent` (generados + reales). "Latencia añadida" = tiempo CPU desde que el fotograma real está listo para presentar hasta su `present` real (incluye copia a FG, presentación del generado y espera de adquisición/pacing); **NO es latencia extremo a extremo medida con hardware** (no disponible aquí). Datos: `docs/evidence/fg/*.csv|md`. `scripts/fg-report.py`.

## F1. 1080p, CPU-bound (render distance 16) — el escenario del monitor del usuario
| Modo | Renderizados FPS | Generados FPS | **Presentados FPS** | Lat. añadida ms | FG GPU ms | Intervalo presentación media / p99 / σ ms |
|---|---|---|---|---|---|---|
| Nativo | **1123.5** (1117/1127/1126) | 0 | **1123.5** | 0.14 | – | 0.89 / 2.03 / 0.26 |
| Solo FG (sin upscaler, F7) | 250.9 (253/252/248) | 250.9 | **501.8** | 2.77 | 1.37 | 1.99 / 3.8 / 0.58 |
| FSR 4.1.1 Quality (sin FG) | 557.1 (563/558/550) | 0 | 557.1 | 0.65 | – | 1.80 / 2.73 / 0.24 |
| FSR 4.1.1 Quality + FG | 219.7 (219/220/220) | 219.7 | **439.5** | 3.36 | 1.22 | 2.28 / 4.5 / 1.00 |
| FSR 4.1.1 Quality + FG, pacing ON | 193.1 (185/191/203) | 193.1 | 386.2 | 4.00 | 1.32 | 2.59 / 4.8 / 0.89 |
Otra sesión (estrés, mismo modo "solo FG", otro estado de reloj de la GPU): 316 renderizados / 632 presentados (`fg_stress.csv`): el FG-solo varía **502–632 presentados** entre sesiones; en ningún caso supera el nativo.

## F2. VSync ON a 100 Hz (el monitor real)
| Modo | Renderizados | Generados | Presentados | Lat. añadida ms | Intervalo media / p99 / σ ms |
|---|---|---|---|---|---|
| Nativo | 100.0 | 0 | 100.0 | 0.11 | 10.0 / 11.1 / 0.7 |
| Solo FG | 50.0 | 50.0 | 100.0 | 8.3 | 10.0 / 14.1 / 3.2 |
Con VSync el monitor ya recibía 100 imágenes/s: FG sustituye 50 reales por 50 reales + 50 generadas, **sin ganancia de FPS mostrados**, con +8 ms de latencia añadida y pacing más irregular (σ 3.2 ms).

## F3. 4K, GPU-bound (Fabulous, render distance 32)
| Modo | Renderizados | Generados | Presentados | Lat. añadida ms | FG GPU ms |
|---|---|---|---|---|---|
| Nativo | 267.7 | 0 | **267.7** | 1.73 | – |
| Solo FG | 88.5 | 88.5 | 177.1 | 8.54 | 3.79 |
| FSR 3.1.5 Quality + FG | 88.3 | 88.3 | 176.7 | 8.48 | 3.68 |

## Conclusión sobre "1000 → 2000 FPS"
- **No es posible** con este hardware/juego, y los datos lo demuestran: presentar 2000 fotogramas distintos/s partiendo de 1000 renderizados exigiría coste FG ≈ 0. El coste medido del FG a 1080p (1.3–2.0 ms de GPU, más copia de entrada/salida, preparación y presentación doble) supera el fotograma nativo (0.89 ms): `presentados_FG ≈ 2 / (T_render + T_FG + T_copias)` ≤ ≈870 FPS incluso con solapamiento perfecto, frente a 1123 nativos; medido 440–632.
- Condición teórica para que FG aumente los FPS presentados: `T_render_nativo > T_FG + T_copias` (≈ más de 2–3 ms por fotograma a 1080p, 5–7 ms a 4K). En Minecraft vanilla con esta GPU no se cumple en ningún escenario medido (1080p: 0.89 ms; 4K Fabulous RD32: 3.7 ms < 4K FG 5+ ms).
- No se anuncia ningún valor de "2000 FPS": el máximo de FPS **presentados** medido fue **632** (y 502 en la tanda de referencia), de los cuales la mitad son generados.
- Limitación de medida: "presentados" cuenta `vkQueuePresent`; el monitor muestra ≤100 imágenes/s.


# Sesión 7 (2026-10-12): línea base repetida y perfil GPU — ver PERFORMANCE_OPTIMIZATIONS.md
Nativo 1134/1149/1143 FPS; FSR 4.1.1 Quality 537/543/538 FPS (1080p, RD16, VSync OFF, 3 reps). Solo FG: 231–244 renderizados / 462–488 presentados (otra tanda 474–517). FSR 4.1.1 + FG: 218–227 renderizados / 436–455 presentados, latencia añadida ≈3.3 ms (software). VRAM FSR 4.1.1 ≈1803 MB vs 1632 MB nativo. Datos: `benchmarks/s7/`.

# Sesión 10 (2026-10-13): presentación por lotes, FG ×3 / ×4
1080p, VSync OFF, render distance 16, escena fija de siempre, RX 9060 XT / RADV 26.2.4. `scripts/bench-suite.sh`: cada modo en 2 sesiones de juego (A, B) × 3 ventanas de 200 ticks; entre paréntesis las 3 ventanas. Los contadores separan **renderizados**, **generados** y **presentados** (`vkQueuePresentKHR`). Con el monitor a 100 Hz el número de imágenes distintas visibles es ≤100/s: "presentados" NO es "FPS observables" (la sonda `present_wait` solo es fiable con VSync; ver `docs/PRESENTATION_ARCHITECTURE.md`). La máquina tiene procesos de usuario en segundo plano (el nativo varió entre 1011 y 1209 FPS durante la sesión según la carga y el muestreador; la tabla usa el muestreador final).

Presentados/s (medias de 2 sesiones): nativo 1201 · FSR 4.1.1 Quality 531 · FG ×2 AMD 466 · **FG ×3 (lotes) 1612** · FG ×3 (ruta anterior) 908 · **FG ×4 (lotes) 1823** · FSR+FG ×2 AMD 415 · **FSR+FG ×3 (lotes) 1232** · FSR+FG ×3 (anterior) 835 · **FSR+FG ×4 (lotes) 1504**.
Tabla completa (renderizados, generados, GPU %, VRAM, núcleos de CPU del juego, p99, 1 % low): `docs/evidence/session10/suite_table.md`.
- Ningún modo con FG supera todavía al nativo en presentados salvo ×3 y ×4 por lotes sin FSR (1,34× y 1,52×). **No se alcanzan 2000/s** (máximo medido 1862) y, aun alcanzándolos, el monitor muestra 100/s.
- GPU ocupada 96–99 % en los modos por lotes (67–72 % en la ruta anterior).
- Memoria: ×3/×4 por lotes +≈60–120 MB de VRAM frente al nativo (imágenes del swapchain 3 → 10 y destinos de generación).
- Latencia (solo VSync 100 Hz, sonda): cola present→visible nativo 62,5 ms; FG ×3 / ×4 (ruta por imagen en FIFO) 33,4 / 33,7 ms. Latencia extremo a extremo no medida.
- Datos crudos: `benchmarks/s10/`.

## Sesión 10, ronda 2 (shader de 64 bits y partículas; monitor vertical, `scripts/bench-suite2.sh`)
Presentados/s, medias de 2 sesiones × 3 ventanas: nativo 1176 · FG ×3 1655 · **FG ×4 1876** (shader de 2 pasadas 1877) · FG ×4 con vectores de partículas (escena sin partículas) 1802 · FSR+×3 1235 · FSR+×4 1454. Tabla: `docs/evidence/session10/suite2_table.md`. Conclusiones: el shader de 64 bits no aporta en juego; los vectores de partículas cuestan ≈3,8 % en una escena sin partículas; **2000/s no se alcanza** (máximo 1891 en una ventana).


## Sesión 12: coste GPU del kernel neuronal a 1080p nativo (medido): 5,04–5,39 ms (todos los tiles), ≈4,0 ms (60 % de tiles); no se ejecutaron benchmarks en juego (no integrado).

## Auditoría 2026-10-22
No hay benchmarks nuevos. El entorno de ejecución actual no expone `/dev/dri`, por lo que no puede producir medidas de GPU, presentaciones ni rendimiento en el monitor vertical. Las cifras de sesión 10 son históricas: `vkQueuePresentKHR` solicitado, no imágenes físicamente visibles; el monitor principal muestra como máximo 100 Hz y el vertical 60 Hz. No se extrapola rendimiento de las pruebas de calidad offline.

## Reanudación 2026-10-22: RX 9060 XT accesible

Las restricciones de la auditoría anterior dejaron de aplicarse: Vulkan enumeró `AMD Radeon RX 9060 XT (RADV GFX1200)`. Escena fija del arnés en el mundo de pruebas, 1920×1080, render distance 16, VSync OFF, monitor vertical HDMI-A-2; **una sesión por modo, dos ventanas de 200 ticks**. El proveedor del reescalador informó `4.1.1` en el runtime AMD. Datos crudos en `docs/evidence/session16/bench_*.testlog`.

| Modo | Renderizados/s | Generados/s | Presentados/s | p99 de cuadro | Despacho FSR GPU |
|---|---:|---:|---:|---:|---:|
| Nativo | 1210 | 0 | 1210 | 1,95 ms | — |
| FSR 4.1.1 Quality | 547 | 0 | 547 | 3,21 ms | 0,827 ms |
| FG ×4, nativo | 467 | 1402 | 1869 | 3,55 ms | — |
| FSR 4.1.1 + FG ×4 | 359 | 1078 | 1437 | 4,21 ms | 0,929 ms |

La cifra de presentados cuenta solicitudes `vkQueuePresentKHR`, no imágenes que puede mostrar el monitor de 60 Hz. La latencia añadida que informa el mod es una estimación software, no una medida extremo a extremo. En replay sintético a 1280×720, el pase FG ×4 tardó **0,5774 ms GPU** para tres generados (mediana de 40). Con `FGX3_INT64=1`, **0,5352 ms**, y los tres RGBA de una escena fueron idénticos byte a byte. En juego, FG ×4 de dos pasadas dio **1869 → 1880** presentados/s en dos sesiones de una ventana de dos repeticiones cada una, con int64 entre ambas **1897**. La variación temporal de ≈11 presentados/s entre sesiones de control no permite atribuir con seguridad los ≈17–28 presentados/s de diferencia a int64. Se conserva como opt-in; no se cambió el shader por defecto.

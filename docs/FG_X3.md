# FG ×3 — backend propio EXPERIMENTAL (sesión 8, 2026-10-12)

**Qué es.** Un generador de fotogramas intermedios **propio** (compute Vulkan, `shaders/fgx3.comp`, `native/src/fgx3_pass.cpp`), totalmente separado del proveedor oficial AMD FG 4.0.1: no crea contexto AMD de FG, no usa el helper de Wine para generar y no contiene código de AMD. Por cada fotograma renderizado presenta **dos fotogramas generados auténticos** (t=1/3 y t=2/3 entre el real anterior y el actual) y después el real: 3 imágenes presentadas por fotograma renderizado. **Desactivado por defecto y etiquetado EXPERIMENTAL** (menú F6 → "Multiplicador FG": x2 AMD oficial / x3 propio; `fgMultiplier` en `config/fsr4linux.json`; F7 sigue activando/desactivando FG). No se duplica ningún fotograma: si no hay fotograma previo válido (primer fotograma, cambio de tamaño, salto de cámara) no se presenta nada extra.
Los planos de profundidad y vectores de movimiento que consume son los del pase de preparación del mod (los mismos que usan FSR y AMD FG) — por eso el bridge de Wine debe estar en marcha para reservarlos, pero la generación no pasa por el helper ni por AMD.

## Algoritmo
Interpolación por vectores con proyección hacia delante: (1) cada píxel del fotograma actual F1 se mueve a `p + (1−t)·mv` (mv = previo − actual, en píxeles); `atomicMax` de la profundidad (reversed-Z) decide quién gana cada píxel destino; (2) se anota el píxel ganador; (3) se recolecta el color de F0 en `c + t·mv` y de F1 en `c − (1−t)·mv` con filtrado bilineal subpíxel y se mezcla con peso t (si la profundidad de F0 en ese punto no es consistente, solo F1); los huecos (desoclusión) se rellenan con la posición en F0 del vecino de fondo más lejano de una ventana 17×17; (4) UI: el color mundo-sin-HUD (copiado antes de dibujar el HUD) se compara con el color final; los píxeles de UI translúcida (delta pequeño) se reaplican de forma aditiva y la UI opaca (delta grande) se toma tal cual del fotograma real. Los vectores incluyen los **vectores por entidad** (siempre activos con ×3).

## Resultados (todo contra fotogramas verdaderos renderizados de forma nativa; escena determinista de `scripts/motion-test.sh`)
**Prototipo aislado** (`tools/fgx3`, Vulkan independiente sin el juego; `scripts/fgx3_dataset.py` + `scripts/fgx3_eval.py`; 10 casos × 2 fotogramas, 1280x720; `docs/evidence/session8/fgx3_prototype_*.txt`):
| | Giro de cámara (yaw+pitch): PSNR / SSIM | Oveja que cruza (caja 0,3): PSNR / SSIM |
|---|---|---|
| **Generado ×3** | **34,04 dB / 0,973** | **31,16 dB / 0,985** |
| Duplicar el real más cercano (NO es generación) | 26,91 / 0,864 | 26,62 / 0,949 |
| Mezcla temporal sin movimiento | 28,38 / 0,870 | 28,37 / 0,947 |
Tiempo GPU: **0,50 ms para los dos fotogramas a 1280x720** (0,25 ms/fotograma; mediana de 30 repeticiones). Memoria: 31,6 MiB a 720p.
**Integrado en el juego** (FG-only, render nativo 1280x720, `scripts/x3-ingame-report.py`, 48 + 50 fotogramas generados; `fg_truth_reports.txt`): giro **34,1 dB / SSIM 0,963** (duplicar 26,8, mezcla 28,3); oveja **31,3 dB / SSIM 0,980** con vectores por entidad (sin ellos 25,9 dB, peor que mezclar: por eso siempre se activan con ×3). Comparación con AMD FG 4.0.1 ×2 en el mismo arnés contra el fotograma intermedio verdadero: giro 33,7 dB / 0,966, oveja 32,0 dB / 0,985 (calidad equivalente; AMD genera 1 fotograma por real, el propio 2).
**Coste en el juego a 1080p** (`FSR4_GPU_TS=1`, 3 mediciones de 200 ticks): **0,68 ms de GPU para los dos fotogramas (0,34 ms cada uno)** frente a 1,2–1,4 ms por fotograma de AMD FG; memoria **58 MiB** a 1080p.
**FPS reales a 1080p** (render distance 16, Fancy, VSync OFF, maxFps 260, escena de `docs/BENCHMARKS.md`, 3 repeticiones, máquina en reposo). Renderizados / generados / presentados son magnitudes SEPARADAS:
| Modo | Renderizados | Generados | **Presentados** | Latencia añadida (CPU) ms | p99 ms |
|---|---|---|---|---|---|
| Nativo 1080p | 1163,5 | 0 | 1163,5 | 0,09 | 1,93 |
| Solo FG ×2 (AMD oficial) | 228 (221/227/236) | 228 | 456 | ≈2,8 | – |
| **Solo FG ×3 (propio)** | 269,2 (260/263/284) | 538,4 | **807,6** (781/790/852) | 2,31 | 5,71 |
| FSR 4.1.1 Quality | 537,2 | 0 | 537,2 | 0,67 | 3,25 |
| FSR 4.1.1 + FG ×2 AMD | 199,6 (192/196/211) | 199,6 | 399,2 | 3,60 | 6,96 |
| **FSR 4.1.1 + FG ×3 propio** | 262,1 (246/261/279) | 524,1 | **786,2** (737/784/838) | 2,57 | 5,74 |
**Lectura honesta.** ×3 presenta 1,77× (solo FG) y 1,97× (con FSR) más imágenes por segundo que ×2 AMD, con latencia CPU añadida menor. Pero en esta escena limitada por CPU el juego nativo a 1080p (1163 FPS) sigue presentando **más** que cualquier modo con FG (máx. 852): el coste fijo de presentar 3 imágenes y del hilo de render (≈3,7 ms/fotograma renderizado con FG) supera al fotograma nativo de 0,86 ms. **No se alcanzan 2000 FPS y no se anuncian.** Donde sí compensa es cuando el renderizado es caro (FSR 4.1.1 Quality: 537 → 786 presentados, +46 %; con ×2 AMD: 399, −26 %).
**Latencia.** No medida extremo a extremo (no hay hardware). Modelo: el fotograma real k se presenta después de sus dos generados, que necesitan que k esté renderizado: añade ≈1 intervalo de render (igual que ×2) + el cómputo de generación (0,68 ms). La latencia CPU "añadida" de la tabla es el tiempo del hook de presentación, no la latencia de entrada.

## Artefactos y límites conocidos (medidos o por diseño)
- Movimiento rígido por caja delimitadora para entidades: sin animación de huesos (patas/cabeza), bordes con desalineación subpíxel visible en la zona de la oveja (31 dB, no 40); máx. 48 entidades.
- Partículas, agua en flujo y objetos translúcidos sin profundidad/vector propio: no medidos; se esperan estelas.
- Desoclusión: relleno con la posición del fondo en F0, sin inpainting; en movimientos grandes puede verse el borde.
- UI: la opaca es exacta; la translúcida se aproxima de forma aditiva (error cuando el mundo de debajo cambia mucho entre F0 y F1).
- Giros muy rápidos / saltos: el vector lineal por píxel asume movimiento uniforme en el intervalo; un salto de cámara >8 bloques reinicia el historial (`fgWarmReset`).
- Intermitente ya existente en el plumbing compartido: una vez de ~12 arranques el autotest CRC de los planos compartidos falló y FG no se activó (reintento correcto); no relacionado con ×3.
- No se ha medido calidad con FSR activo frente a nativo verdadero (el generador consume la imagen escalada; el techo de calidad lo marca FSR).

## Reproducir
`scripts/build-native.sh && scripts/build-shaders.sh && scripts/install-runtime.sh`; prueba numérica `build/cmake/fgx3_test` (textura traslada + bloque de HUD + primer plano con desoclusión; control negativo `FGX3_TEST_BADMV=1` debe FALLAR); prototipo: grabar `FSR4_ENTITY_MV=1 FSR4_MOTION_PLANES=DIR FG=true VEL=0.02 YAW_RATE=0.1 PITCH_RATE=0.0667 scripts/motion-test.sh native OUT`, luego `scripts/fgx3_dataset.py`, `tools/fgx3/run.sh CASO 1280 720`, `scripts/fgx3_eval.py`; en el juego: `FSR4_X3_CAPTURE=1 FG=true FGMULT=3 FA=30 FB=30 VEL=0.06 YAW_RATE=0.3 PITCH_RATE=0.2 scripts/motion-test.sh native A` y la misma escena a 1/3 de paso sin FG (`FG=false VEL=0.02 YAW_RATE=0.1 PITCH_RATE=0.0667`), `scripts/x3-ingame-report.py A B`.

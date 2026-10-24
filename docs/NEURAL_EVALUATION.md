# Evaluación en escenas NO vistas (TEST) — resultado: NO APROBADO

## Auditoría 2026-10-22
Sin evaluación nueva de modelos: `sbg8` y `train3` continúan rechazados por los criterios existentes. Los datasets `v3_part*_A/B` empaquetados pertenecen al régimen anterior de ~12 ticks/fotograma; no son una validación del corrector neuronal con vectores medidos a `tick rate 1`. Las grabaciones nuevas de sesión 15 (humo y esporas) sí tienen planos de profundidad/MV A/B y timestamps para análisis posterior. `tools/particles/recording_audit.py` comprueba la integridad mínima de una grabación antes de intentar construir datasets. TEST3 no se abrió ni se utilizó para seleccionar modelos.
`evaluate.py runs/selres8h.best.pt --split test --max 40` (42 fotogramas/escena, verdad = fotograma real; logs `runs/eval_selres8h_test*.log`). PSNR de luminancia, G = FG ×3/×4 actual.
| Escena | PSNR G | PSNR red | Δ medio | peor fotograma | fotogramas peores >0,1 dB | gross% (≥16 niveles) |
|---|---|---|---|---|---|---|
| disocclusion | 38,40 | 37,33 | **−1,06** | **−6,27** | 24/42 | 0,84→1,81 |
| transparency | 38,77 | 38,68 | −0,09 | −1,00 | 22/42 | 0,82→1,09 |
| particles | 22,26 | 23,05 | **+0,79** | +0,41 | 0/42 | 9,33→11,91 |
| fastentity | 37,22 | 37,05 | −0,17 | −3,78 | 23/42 | 0,72→1,06 |
SSIM baja en las 4 escenas (p. ej. particles 0,836→0,827). Con `--selective` (solo tiles marcados) el resultado es prácticamente igual. Parpadeo mejora en algunas (particles 0,82→0,68) y empeora en disocclusion sin selectivo.
Umbral de confianza `--hard` (solo VAL): 0,6 → +0,02 dB; 0,8/0,9 → +0,00 dB: no existe punto de operación con ganancia y sin regresión.
**Conclusión (criterio 7 de aceptación):** la ganancia de +0,7 dB en VAL no generaliza; en escenas de prueba el modelo empeora disocclusion, entidades rápidas y transparencias (regresión grave en el peor fotograma) y solo ayuda a partículas. No se integra en Minecraft, no se añaden controles al menú F6 ni se activa. Causas probables (no verificadas por ablación): dataset pequeño (252 fotogramas, 1 escena de validación), la verdad fuera de {G, A, B, C0, C1, blend} no es alcanzable por selección, y en desoclusión ningún candidato es correcto.
Pendiente para un intento futuro: más escenas de entrenamiento, validación multi-escena, salida generativa para huecos, versión cooperative-matrix FP16 para bajar el coste, mejores vectores de partículas (ya ayudan sin IA, ver PARTICLE_MV.md).

# Sesión 12 — evaluación en escenas independientes
Métricas (`evaluate.py`, 14 fotogramas por escena de 42, PSNR de luminancia sobre el fotograma real; SSIM con acumulación float64 tras corregir un fallo de precisión; ghost% y gross% definidos en `NEURAL_ACCEPTANCE.md`). G = FG actual, R = regla "hueco→mezcla" (sin aprendizaje), h1 = `sbg8` entrenado.
## TEST2 (sellado, evaluado una vez) — ΔPSNR sobre G, peor fotograma, ΔSSIM, Δgross%, ghost% G→salida
| Escena | R ΔPSNR (peor) | h1 ΔPSNR (peor) | h1 ΔSSIM | h1 Δgross% | h1 ghost% |
|---|---|---|---|---|---|
| occl_d | +2,08 (+0,00) | +1,57 (+0,06) | +0,0023 | +0,21 | 0,166→0,242 |
| fast_d | +0,66 (+0,00) | +1,17 (+0,51) | +0,0003 | +0,11 | 0,196→0,207 |
| part_d | +0,07 (+0,00) | +0,58 (+0,44) | **−0,0080** | **+1,45** | 2,28→2,57 |
| transp_d | +0,53 (+0,00) | **−0,24 (−0,68)** | −0,0004 | +0,08 | 0,075→0,113 |
| turn_c | +1,36 (+0,00) | +1,82 (+0,36) | +0,0059 | −0,40 | 0,447→0,900 |
| combo_c | +0,16 (+0,01) | +0,84 (+0,53) | +0,0011 | −0,09 | 0,921→1,156 |
| media | +0,81 | +0,96 | | | |
## VALIDACIÓN (11 escenas) para h1: ΔPSNR +1,04 / +1,58 / +1,05 / +0,83 / +1,17 / +1,41 / +1,01 / +0,65 / +0,99 / +1,21 / +0,81 (todas positivas, media +1,07; peor fotograma −5,71 dB en un fotograma de `disocclusion`). Partículas: gross% 9,14→11,77 y SSIM 0,8337→0,8242.
## Veredicto frente a los criterios fijados (NO aprobado)
- R no regresa PSNR en ninguna escena (peor 0,00) pero sube el ghost proxy (×1,1–2,8) y no mejora partículas.
- h1 mejora PSNR en 5/6 escenas (media +0,96 dB, mejor que R en 4) y supera a R en escenas de giro/combinadas, pero (i) regresa en `transp_d` (−0,24 dB, 7/14 fotogramas peores), (ii) el error grosero y el SSIM empeoran en partículas, (iii) el ghost proxy sube en 5/6 escenas y (iv) en `disocclusion` hubo un fotograma −5,7 dB. Evidencia visual: `docs/evidence/neural/test2_part_d_gross.png` (columnas: G | R | red | verdad; abajo error ×4) muestra el humo duplicado/semitransparente en la salida de la red; `test2_transp_d_worst.png`, `test2_turn_c_best.png`.
- Un TEST anterior (6 escenas, ahora validación) dio R +0,66 y b0g(prior 3) +0,86 de media, 6/6 positivas: la mejora de PSNR es reproducible en 22 escenas, pero a costa de promediar imágenes (cobertura de MSE) que el proxy de ghosting y la inspección visual penalizan. El PSNR no es una medida suficiente de calidad aquí.
Parpadeo/consistencia temporal: no se midió en esta sesión (el submuestreo de fotogramas no permite secuencias completas); queda pendiente.

## Sesión 13 (en curso, pausada) — métricas de ghosting, vectores de partículas, selección dura
- **Métricas** (`NEURAL_GHOST_METRICS.md`): con ellas `s12_h1` (PSNR +0,47 dB medio) empeora MAE (+0,10), error grosero en movimiento (+1,4 pts; +5…+8 pts en partículas), mezcla de dos imágenes (+0,69 pts) y nitidez (−0,07). La ganancia de PSNR era de MSE. La regla R tampoco mejora el relleno de huecos (`hole_mae` +1,6). Criterios v2 fijados antes de TEST3 (`NEURAL_ACCEPTANCE.md`).
- **Vectores de partículas (A/B de la misma grabación, 4 escenas de validación + 1 diagnóstica):** con vectores el generador es PEOR dentro de las partículas en todas las escenas (MAE en partículas +2,7…+10, error grosero +6…+12 pts; fuera de las partículas sin cambio). El PSNR global baja 0,05…0,6 dB y el humo sale duplicado (inspección visual, `ab_particle_regions.txt`). Conclusión provisional: la implementación actual (disco, velocidad de tick) NO ayuda con partículas translúcidas; sigue opt-in/off. No se ha diagnosticado la causa (planos crudos borrados; el MV de las características satura en ±2 px). Nota: `t3_part1` se miró solo para este diagnóstico del generador, no para elegir modelo.
- **Selección dura sin mezclas (`train3.py`, `sbng8`, straight-through, L1 + hinge vs G, validación con criterios v2, 21 escenas de validación):** k1 (ce 0,3) −0,3…−0,4 dB, k2 (ce 1,0) −0,8 dB: no mejora; quita la mezcla (Δmix% −0,3) pero empeora PSNR y MAE. Las variantes k3 (penalización de mezcla), k4 (contexto) y k5 (16 canales) murieron a la iteración ~500 (proceso interrumpido; sin resultado).
- **Datos nuevos:** 10 escenas A/B (`v3_*` validación, `t3_*` TEST3 sellado, sin evaluar) en `~/Escritorio/FSR4-neural-data` (`*_A`, `*_B`). Planos crudos de esas escenas borrados tras construir los datasets.
- **Estado:** ningún modelo aprobado; no integrado; TEST3 sin tocar.

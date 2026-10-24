# Criterios de aceptación del corrector (fijados el 2026-10-14, ANTES de evaluar el TEST de la sesión 12)
Un modelo o regla R se acepta frente al generador actual G solo si, en **cada** escena de VALIDACIÓN y de TEST (luminancia, fotogramas completos 1280x720, verdad = fotograma real intermedio):
1. ΔPSNR medio de la escena ≥ 0,00 dB y peor fotograma ≥ −0,5 dB.
2. ΔSSIM de la escena ≥ −0,001.
3. gross% (píxeles con error ≥16 niveles) no sube más de 0,10 puntos absolutos.
4. ghost% (proxy: píxeles donde la salida ≈ mezcla temporal [<6 niveles], la verdad está lejos de la mezcla [≥24] y el error ≥16) no se multiplica por más de 1,25 (ni sube más de 0,05 puntos absolutos).
5. Ganancia media entre escenas ≥ +0,30 dB.
Selección de checkpoint en validación: puntuación = ganancia media por escena + 2·mín(0, peor escena).
Parada de experimentos: un candidato se abandona si tras 2 000 iteraciones su puntuación de validación es < +0,10 o si empeora una escena de validación > 0,3 dB sin recuperarse en las 1 000 siguientes.
El TEST (occl_c, fast_c, part_c, transp_c, turn_b, combo_b) se mide una vez por familia final, nunca para elegir arquitectura, umbral ni hiperparámetros.

---
# Criterios v2 (sesión 13) — fijados ANTES de grabar/evaluar TEST3 y antes de entrenar ningún modelo nuevo
Motivo: el PSNR premió modelos que promedian (ver `NEURAL_GHOST_METRICS.md`). Los criterios v1 siguen siendo necesarios; se AÑADEN estos, por escena, respecto al generador G de la MISMA variante (A o B de vectores de partículas), medidos con `ghost_eval.py`:
1. ΔPSNR ≥ 0 por escena, peor fotograma ≥ −0,5 dB, ganancia media sobre escenas ≥ +0,30 dB (v1).
2. ΔMAE ≤ +0,02 (no empeora el error absoluto) y ΔMAE_móvil ≤ +0,05; media de ΔMAE_móvil ≤ −0,05 (debe MEJORAR de verdad).
3. Δgross_móvil ≤ +0,30 pts por escena.
4. Δmix% ≤ +0,15 pts y Δdupcorr ≤ +0,01 por escena (no más imagen doble).
5. Δhf_ratio ≥ −0,02 por escena (no más desenfoque) y Δtdiff ≤ +0,02 (no más parpadeo).
6. ΔSSIM ≥ −0,001 (v1) y sin regresión grave en transparencias (ΔPSNR ≥ 0 y ΔMAE ≤ 0,02 en las escenas transp_*).
7. Selección de checkpoint SOLO con VAL y con la puntuación v1 ampliada: media(ΔPSNR) − 2·peor(0,ΔPSNR) − 5·media(max(0,ΔMAE_móvil)) − 0,5·media(max(0,Δmix%)).
8. TEST3 (v3: t3_part1, t3_part2, t3_transp, t3_occl, t3_fast, t3_combo, grabados con A/B de vectores de partículas) se evalúa UNA vez por familia final; no se usa para elegir nada. Un fallo en un solo criterio de una sola escena = NO aprobado.
9. Coste: el modelo integrado debe costar < 1,5 ms GPU a 1080p (con salto de teselas) o se descarta por coste; ahorro y calidad se informan por separado.
Experimentos de generador (independientes de la red): A = G sin vectores de partículas; B = G con vectores de partículas; C = A/B + regla tradicional R; D = A/B + red. Se compara siempre D contra su propio G, y B contra A con las mismas métricas y la misma verdad.

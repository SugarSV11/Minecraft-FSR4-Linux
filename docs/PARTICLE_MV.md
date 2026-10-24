# Vectores de movimiento de partículas (EXPERIMENTAL, sesión 10)

**Problema.** Las partículas de Minecraft (llama, humo, polvo…) son quads translúcidos sin vector de movimiento ni profundidad propia: un píxel de partícula hereda el movimiento de cámara del fondo y el generador de fotogramas las arrastra mal. En el conjunto de escenas de la sesión 9 la escena de partículas era la peor (≈21,8 dB, 8,5 % de píxeles con error grosero) y `tools/ai/error_budget.py` muestra que ese error no se arregla con filtros locales (ni con redes pequeñas).

**Qué se hizo (API real de 26.3 verificada en el código descompilado).** `SingleQuadParticleMixin` captura, en `SingleQuadParticle.extractRotatedQuad(QuadParticleRenderState, Quaternionf, float×4)` (todas las partículas de quad pasan por ahí), la posición interpolada relativa a la cámara, el tamaño (`getQuadSize`) y la velocidad por tick (`x − xo`); `ParticleEngineMixin` reinicia la lista al empezar `ParticleEngine.extract`. `ParticleMotion.build` calcula para cada partícula visible la semi-extensión en píxeles y el desplazamiento desde el fotograma renderizado anterior (velocidad × tiempo de fotograma anterior/50 ms: primer orden). El modo 2 de `shaders/fsr4_prep.comp` (un grupo de trabajo por partícula, hasta 4096, `PrepPass::setParticles`) escribe, para los píxeles de un disco inscrito (≈70 % de la semi-extensión) que están por delante del búfer de profundidad, la profundidad de la partícula y su vector completo (cámara + desplazamiento) en los planos compartidos que usan FSR, AMD FG y el generador propio. Activación: `FSR4_PARTICLE_MV=1` (opt-in).

**Resultado (escena de partículas, 3 repeticiones independientes de grabación + reproducción por el paso real, 49 casos cada una; `docs/evidence/session10/particles_eval_*.txt`).** PSNR del generado menos PSNR de la mezcla temporal (positivo = mejor que mezclar):
| | cámara fija | cámara móvil |
|---|---|---|
| sin vectores de partículas | −0,75 / −0,82 / −0,71 dB | +0,23 / −0,01 / +0,04 dB |
| **con vectores de partículas** | **+0,55 / +0,53 / +0,51 dB** | **+1,33 / +1,29 / +1,23 dB** |
PSNR absoluto del generado: 21,8–22,0 → 22,95–23,04 dB (fija), 21,4–22,2 → 22,7–22,8 dB (móvil); error grosero 5,7 % → 5,1 % (fija), 5,7 % → 5,3 % (móvil); el parpadeo temporal se acerca a la verdad (×0,37 → ×0,79 en fija). **SSIM baja ≈0,016** (0,885 → 0,868 fija; 0,858 → 0,842 móvil): partículas con borde duro/discos recortados. Mejora real pero modesta; no resuelve partículas por completo.

**Coste.** En la escena de benchmarks (sin partículas) FG ×4 con el modo activado: 1795 / 1810 vs 1865 / 1887 presentados/s sin él (≈ −3,8 %, causa no aislada: no hay despachos de partículas cuando no hay ninguna). Por eso queda **opt-in**. No se midió el coste GPU en la escena con partículas.

**Validación.** Capa Khronos + sincronización con llamas reales, ×3 solo y FSR 4.1.1 + ×3, con pantalla completa: sin hallazgos nuevos.

**Límites.** Rotación de cada quad (roll) ignorada; el desplazamiento usa la velocidad del tick (sin gravedad/fricción intra-tick); subclases que sobrescriban `extractRotatedQuad` sin llamar a `super` no se capturan; la máscara de disco es una aproximación (no se muestrea el alfa del sprite); partículas que no son quads (objetos recogidos, etc.) sin vectores; escrito para el pipeline de planos actual (render-res); superposición de partículas sin orden (gana una arbitraria).


## Sesión 15: causa del humo duplicado (DEMOSTRADA) y corrección
**Causa.** El desplazamiento de cada partícula entre fotogramas se calculaba como `velocidad/tick × min(4, ms del fotograma / 50)`: tiempo de pared, con tope de 4 ticks. El arnés de grabación (`scripts/x3-scene.sh`, capturas + volcado de planos) trabaja a **≈0,6 s por fotograma** (≈12 ticks de juego), así que el vector no era la velocidad real y, en cuanto el tick rate ≠ 20, el juego está pausado/congelado o hay picos >200 ms, tampoco lo sería en juego. Todos los A/B de partículas de las sesiones 10–14 (y los datasets neuronales con partículas) se grabaron en ese régimen. No fue el signo, ni el eje Y, ni la profundidad.
**Corrección (2 pasos).** (1) Primer intento: `DeltaTracker.getGameTimeDeltaTicks()` en lugar de ms/50. Mejoraba `/tick rate`, pero **con `/tick freeze` seguía escribiendo vectores falsos** (partículas quietas, B 12,08 de MAE frente a A 2,80: el delta de ticks no es 0 con el juego congelado). (2) Corrección definitiva: **desplazamiento medido, no predicho**: `ParticleMotion` guarda por partícula (mapa por identidad) su posición mundial interpolada del fotograma anterior y el vector es `pos_actual − pos_anterior` (0 si la partícula es nueva). Es exacto con cualquier tick rate, pausa, congelación, picos de latencia, gravedad o fricción, y no depende de relojes. La grabación admite `TICKRATE=n` y `TICKCMD="freeze"` (`/tick …`).
**Evidencia (generador real `fgx3_replay`, grabaciones reales con A/B, `tools/particles/real_ablation.py`, 30 casos × 3 fotogramas generados, error dentro de la máscara de partículas; `docs/evidence/session15/`).** Escena `v3_part1` (humo de hoguera), MAE en la máscara / PSNR en la máscara:
| variante | MAE | gross % | PSNR |
|---|---|---|---|
| sin vectores de partícula (A) | 14,05 | 22,7 | 15,67 |
| **con vectores, escala correcta (B)** | **10,72** | **21,1** | **17,65** |
| B solo vector, sin profundidad | 11,13 | 19,5 | 16,99 |
| escala ×0,5 / ×1,5 | 12,61 / 13,58 | 23,7 / 25,3 | 16,78 / 16,40 |
| **escala ×6,8 (= estimación antigua en esta grabación)** | **19,68** | 32,7 | 14,68 (`excess_c` 0,022 = bordes duplicados) |
| eje Y invertido | 16,50 | 29,7 | 15,51 |
| solo componente X | 14,05 | 25,9 | 16,13 |
Mezcla temporal: 15,32. Fuera de la máscara no hay cambio (0,55 → 0,54). La escala 1,0 y el signo actual son los óptimos (barrido de escala y signo). Réplica independiente en `v3_part2` (esporas): A 17,56 → B 15,40 (+1,6 dB); escala ×6,8: 21,14. Medida directa contra el flujo óptico de los fotogramas verdaderos (`tools/particles/mv_vs_flow.py`): error del vector 2,05 px (solo cámara) → **0,74 px** con el vector de partícula, mejor en el 78 % de los píxeles, pendiente en Y = 1,01 (escala exacta). En el régimen antiguo (12 ticks/fotograma) el mismo análisis daba pendiente 0,41 y B mejor solo en el 40 %.
**Prueba sintética determinista** (`tools/particles/synth_particles.py`, generador real): con humo translúcido el vector por disco **empeora** siempre frente a ignorarlo (MAE 0,39 → 2,29 con alfa 0,3; 0,51 → 1,62 con 8 partículas), porque un único vector por píxel arrastra el fondo visible; el disco recortado mejora solo si la partícula es casi opaca. Un vector no puede representar a la vez humo y fondo: es una limitación del modelo, no un error de signo/escala. En las escenas reales el vector sí gana neto (arriba). Pendiente: máscara por alfa del sprite.
**Estado.** Corrección integrada (tests Java, `prep_test`, `fgx3_test` PASS). `FSR4_PARTICLE_MV` **sigue desactivado por defecto**: faltan el coste GPU en juego y el −3,8 % sin partículas (no medidos aquí).
**Aviso para evaluaciones previas:** con fotogramas a 12 ticks (≈0,6 s) el movimiento de las partículas es 10–20× mayor que en juego real (2–10 ms por fotograma): los datasets de partículas anteriores no representan el régimen de uso.

**Resultados con el desplazamiento medido (misma herramienta, `v3_part1`, 20–30 casos; `docs/evidence/session15/particle_ablation_measured_*.txt`):**
| régimen | A (sin vectores) MAE | B (vectores) MAE | nota |
|---|---|---|---|
| `/tick rate 1` (0,59 ticks/fotograma) | 16,30 | **13,93** | escala ×1 óptima; ×6,8: 20,50; Y invertido 19,12 |
| `/tick rate 20` (≈12 ticks/fotograma, régimen de las sesiones 10–14) | 20,96 | **18,70** | antes (estimador antiguo) era peor que A |
| `/tick freeze` (partículas quietas, cámara en movimiento) | 2,68 | 2,57 | vector ≈ 0: ya no hay vectores falsos |
| `v3_part2` esporas, `/tick rate 1` (ventana 1080×720) | 18,83 | 18,83 (PSNR +0,4 dB) | neutral; escala ×1 sigue siendo la mejor |
Pausa de juego (menú): no medida por separado; con el desplazamiento medido el vector es 0 mientras la partícula no se mueva (mismo mecanismo que `/tick freeze`).
Nota: las grabaciones en el monitor vertical pueden salir con ventana de 1080 px de ancho; `real_ablation.py` detecta el tamaño.

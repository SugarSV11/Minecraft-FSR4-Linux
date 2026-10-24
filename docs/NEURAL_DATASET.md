# Dataset del corrector neuronal (sesión 11)
Código: `tools/ai/neural/dataset.py`. Datos (10 GB, fuera del repo): `~/Escritorio/FSR4-neural-data`; capturas origen: `~/Escritorio/FSR4-evidence-session11/<escena>/cases`.
Capturas con el arnés de pruebas del proyecto (replay por el pase real de FG, verdad = fotograma real intermedio de la MISMA grabación, ejecución densa a 1/2 o 1/3 de paso). No se tocaron mundos personales.
Resolución 1280x720, RGB 8 bit, orientación de los render targets (abajo-arriba). 22 canales por muestra: G (FG actual), A/B (muestras de F0/F1 en la posición deformada), C0/C1 (reales vecinos), F (tiene-fuente / profundidad-consistente / hueco-rellenado), M (mv x, mv y, cambio de profundidad relativo), T (posición temporal). Objetivo Y = fotograma verdadero intermedio.
| Split | Escenas (completas, sin secuencias compartidas) | Fotogramas |
|---|---|---|
| TRAIN | baseline, vegetation, water, animals, night, monsters | 252 |
| VAL | mixed | 42 |
| TEST | disocclusion, transparency, particles, fastentity | 42 evaluados por escena (--max 40) |
Cobertura: cámara móvil, animales, monstruos, entidades rápidas, partículas, agua, transparencias, vegetación, desoclusión, noche. Máscaras de oclusión/inconsistencia = canales F y `flag_mask` (models.py).
Límites: una sola semilla de mundo/escena por categoría, 1280x720 (no 1080p), sin jugadores reales en la muestra; el dataset es pequeño (252 fotogramas de entrenamiento).

## Sesión 12 — dataset ampliado (25 escenas de entrenamiento/validación + 6 de TEST sellado)
Auditoría (`tools/ai/neural/oracle_audit.py`, `runs/oracle_audit*.log`): 11 escenas originales con 252+ fotogramas; todas a 1280x720. Se grabaron 20 escenas nuevas con `scripts/x3-scene.sh` (14 + 6, nuevos casos `occl_*`, `fast_*`, `part_*`, `transp_*`, `water_b`, `animals_b`, `turn_*` (giros bruscos, yaw 0,9–1,4), `combo_*` (cámara + entidades + partículas + transparencias)), con lluvia/noche en varias. 42 fotogramas generados por escena (casos de stride 4: FG ×4, t = 0,25/0,5/0,75), verdad = fotograma real de la misma grabación. Total 31 escenas, 1 302 fotogramas, 28 GB en `~/Escritorio/FSR4-neural-data` (fuera del repo). Monitor vertical (HDMI-A-2, detectado con kscreen) vía script de KWin; las ejecuciones nunca tocaron mundos personales.
Incidencias reales: 4 escenas salieron a 1080x720 por una carrera con el script de KWin (`fgx3_replay` hacía segfault con ficheros de tamaño distinto); se detectó, se regrabaron comprobando la anchura, y todas las del dataset son 1280x720. Se borraron los planos crudos (profundidad/MV, ~1,1 GB/escena) de session12 tras construir el dataset para liberar disco (los PNG de color se conservan).
Particiones (por escenas completas):
| Split | Escenas |
|---|---|
| TRAIN (14) | baseline, vegetation, water, animals, night, monsters, occl_b, fast_b, part_b, transp_b, water_b, animals_b, turn_a, combo_a |
| VAL (11) | mixed, disocclusion, transparency, particles, fastentity + occl_c, fast_c, part_c, transp_c, turn_b, combo_b (estas 6 formaron un TEST que se evaluó una vez y quedó "consumido") |
| TEST2 sellado (6) | occl_d, fast_d, part_d, transp_d, turn_c, combo_c — evaluado UNA vez al final |
Hallazgo del oráculo (`oracle_audit.py`): en TODAS las escenas, elegir por píxel entre {G, A, B, C0, C1, mezcla} daría +4…+15 dB, y solo 0,05–10 % de los píxeles no tiene ningún candidato a ≤16 niveles: la información suele estar; lo difícil es decidir cuál usar. Añadir dos fotogramas reales extra sin deformar (k−4, k+8) sube el oráculo +1,5…+3 dB y reduce 35–40 % los píxeles sin candidato (`temporal_oracle.py`): la historia temporal no es el cuello de botella.

## Sesión 15: datasets con movimiento temporal incorrecto
Las escenas con partículas grabadas hasta la sesión 14 (`part*`, `v3_part*`, `t3_part*`, `combo*`, `mixed`, `particles`, incluidas las variantes A/B) se grabaron a ≈0,6 s por fotograma (≈12 ticks de juego) y, las B, con vectores de partícula mal escalados. Afectadas: todas las `*part*` (A y B), y cualquier escena con `area_effect_cloud`/hogueras. NO afectadas en lo que respecta a vectores de partícula: escenas sin partículas (occl, fast, turn, transp, water, animals, vegetation), aunque comparten el mismo régimen de 12 ticks/fotograma (más movimiento de agua/animales que en juego real). No se ha regenerado nada todavía; reglas para regrabar: `AB=1 TICKRATE=1 scripts/x3-scene.sh ESCENA DIR` con DIR en el disco de datos.

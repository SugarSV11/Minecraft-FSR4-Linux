# Entrenamiento (`train.py`, CPU/PyTorch, Ryzen 7 5700; ROCm no se probó en esta GPU, no hizo falta para estos tamaños)
Hiperparámetros reales (logs en `tools/ai/neural/runs/*.log`): Adam lr 2e-3 coseno, 2 500 it, lote 12, recortes 96x96 con muestreo de importancia hacia píxeles marcados, patience 5 (early stopping por VAL, validación cada 500 it), semilla 0 (y s1/s2 para selres16), hinge 2.0, checkpoints `*.pt` / `*.best.pt`, `--resume`.
Pérdida L = media(w·|out−Y|) + 0,5·|sobel(out)−sobel(Y)| + 0,2·media((1−flag)·|out−G|) + 2,0·media(relu(|out−Y| − |G−Y|)).
- w = 1+3·[|G−Y|≥16/255]: el 70–99 % del error está en <9 % de los píxeles; L1 es robusta y más nítida que L2.
- Término de bordes: evita el desenfoque. Término "keep": fuera de la zona marcada la salida debe ser G (localidad). Hinge de no-regresión: penaliza solo píxeles empeorados respecto a G.
- Consistencia temporal NO tiene término propio (solo se mide: parpadeo en la evaluación).
Validación (mixed): G 22,81 dB → selres8h 23,55 (+0,74 dB, it 1500); selres16h_s1 +0,77 (it 2000). TEST nunca se leyó al entrenar ni para elegir hiperparámetros (el umbral `--hard` se probó solo en VAL).

## Sesión 12 — entrenamiento
Entorno: ROCm funciona en esta GPU. venv aislado `~/neuralvenv` (torch 2.9.1+rocm6.4, 15 GB; `--device cuda` en `train.py`, reanudable con `--resume`); sin tocar drivers del sistema. GPU ≈ 8× más rápida que la CPU con lotes mayores (lote 24, recortes 128).
Criterio de selección de checkpoint, fijado antes de los resultados (`docs/NEURAL_ACCEPTANCE.md`): fotogramas completos de todas las escenas de validación, puntuación = ganancia media por escena + 2·mín(0, peor escena). Registros: `tools/ai/neural/runs/s12_*.log`.
Hiperparámetros del modelo final (`s12_h1`): sbg8, MSE ponderada (×4 donde G falla ≥16 niveles) + 0,1·bordes + 0,05·CE hacia el mejor candidato + 1,0·hinge de no-regresión respecto a R (penaliza solo píxeles peores que R), Adam lr 1e-3 coseno, 3 000 it, lote 24, recortes 128 UNIFORMES (`--bias 0`), semilla 0.
Lecciones medidas (cada una con su log):
1. Muestreo de recortes sesgado a zonas marcadas (0,7) hacía que la red usara A/B/C0/C1 en toda la imagen a fotograma completo (−1,9 dB incluso en escenas de entrenamiento): con muestreo uniforme todas las escenas de validación pasan a positivas.
2. La rama `--loss mse` devolvía antes del término hinge (error mío, corregido): los runs b0/b0g/g2/m*/r*/u1/c1 NO tuvieron hinge; h1 sí y fue el primero estable en 11/11 escenas.
3. L1 puro (`x1`, `r1`) no se mueve de R (gradiente casi nulo). Residual sobre R con MSE (`r2/r3`): −3 a −8 dB en `disocclusion`/`transparency` (regresa).
4. Término anti-ghosting (`h2/h3`, proxy de `NEURAL_ACCEPTANCE.md`): +0,19/+0,14 de puntuación, peor que h1 (+0,87), con `fastentity` negativa.
5. Sin candidato de mezcla (`n1–n3`): +0,3 dB, regresiones en `disocclusion` y `fastentity`: la mezcla es lo que estabiliza.
6. Un prior hacia R en inferencia (`--prior`) mejora monótonamente la puntuación de validación del modelo viejo hasta converger a R: ese modelo no aportaba nada sobre R.
Criterio de parada aplicado: se abandonaron los runs con puntuación de validación negativa tras 1 000–2 000 it o con una escena regresando >0,3 dB.

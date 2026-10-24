# Continuación de Neural Enhancement en Beta 2

La IA neuronal propia queda **pospuesta**, no abandonada. Retomar en la rama `experimental/ai-performance` (HEAD inicial `8b26e6e` y cambios de investigación sin commit preservados). `beta/1.0` es solo la rama jugable; no seleccionar ni entrenar modelos allí. No hacer push ni mover los archivos grandes por el mero corte de beta.

## Ubicaciones que deben conservarse

- Originales: `../FSR4-neural-data` y `../FSR4-evidence-session{8,9,11,12,13,15}` respecto al repositorio original `Minecraft-FSR4-Linux`.
- Copias verificadas: `/run/media/system/steam/NeuralCraft-Data/datasets/FSR4-neural-data` y `/run/media/system/steam/NeuralCraft-Data/evidence/FSR4-evidence-session{8,9,11,12,13,15}`. Las marcas `.verified-*` indican la copia contrastada en sesión 15. `evidence/session15-new` conserva grabaciones a un tick por cuadro, incluidos PNG, profundidad `d/da`, movimiento `m/ma` y cámara. Hay directorios `checkpoints`, `training`, `cache` y `benchmarks` en la raíz Steam; los checkpoints de modelos detectados están en el repositorio, no en el directorio Steam `checkpoints`.
- Checkpoints y logs de entrenamiento: `tools/ai/neural/runs/*.pt`, `*.best.pt`, `*.log`, además de evaluaciones JSON en el mismo directorio. Se comprobaron **39** archivos `*.best.pt`, entre ellos `s14_k3.best.pt` y `s14_k4.best.pt`. Código: `tools/ai/neural/`, `tools/ai/ncpaths.py`, `tools/particles/`, `tools/disocclusion/`, `scripts/nc-env.sh`; prototipos Vulkan en `native/src/neural_pass.cpp`, `shaders/neural_sel.comp` y `tools/coopmat/`.
- Entorno ROCm histórico: `~/neuralvenv/v` con PyTorch ROCm 6.4 según `NEURAL_TRAINING.md`; **no existe en este entorno al auditar**, por lo que habrá que restaurarlo o recrearlo sin alterar checkpoints. No usar el PyTorch CUDA del host como sustituto silencioso.
- Resguardo del estado sin commit anterior a Beta 1: `../beta1-prebranch-safeguard-20261023-uReV2a/`; copia Git previa `../Minecraft-FSR4-Linux-backup-20261012-1542.git-only`.

`tools/ai/ncpaths.py` y `scripts/nc-env.sh` resuelven la copia Steam verificada; `NC_DATA_ROOT` permite elegir otra raíz. Antes de cualquier entrenamiento, comprobar montaje y rutas, conteos y hashes de muestras, sin usar herramientas de limpieza ni sobrescribir copias.
En la auditoría de Beta 1 existían los siete directorios originales y sus copias Steam; `baseline_meta.json` tenía el mismo SHA-256 en ambas. No se borró ningún checkpoint ni se abrió TEST3.

## Datos y modelos

`NEURAL_DATASET.md` define TRAIN (14 escenas en sesión 12), VALIDATION (11), TEST2 (6, ya evaluado una vez) y el TEST original de sesión 11. Los datos `v3_*` A/B amplían validación; los seis `t3_*` A/B son TEST3. Conservar todas estas particiones y los fotogramas reales intermedios, planos, máscaras y metadatos. Los datasets antiguos con partículas se grabaron a ~12 ticks/cuadro y sus vectores B tenían escala errónea; las nuevas grabaciones de `session15-new` permiten estudiar el régimen de un tick/cuadro antes de comparar modelos.

- Sesión 11: `selres8h` (3 417 pesos, selector) mejoró ~0,74 dB en VAL pero en TEST cayó 1,06 dB en desoclusión, 0,17 en entidad rápida y 0,09 en transparencia; solo partículas ganó 0,79. No pasó aceptación.
- Sesión 12: familias `sb8`, `sb16`, `sbctx8`, `sbunet12`, `resb16`, `sbg8`, `sbn8/sbng8` y variantes guardadas en `runs/`. `sbg8`/`s12_h1` (3 593 pesos) ganó +0,96 dB medio en TEST2, pero `transp_d` perdió 0,24 dB; partículas empeoraron SSIM y ghosting. No aprobado. La regla sin aprendizaje R ganó +0,81 dB, pero también incrementó el proxy de ghosting.
- Sesiones 13–14: `train3.py` con selección dura `k1` (~−0,3 a −0,4 dB) y `k2` (~−0,8 dB) no mejoró al generador. `s13_k3/k4/k5` se interrumpieron cerca de it 500 sin checkpoint reanudable; `k5` duplicaba `k4`. Relanzamientos `s14_k3.best.pt` (−0,42 dB en VAL) y `s14_k4.best.pt` (−0,25 dB en VAL) sí existen y terminaron por paciencia, pero ninguno supera G. Conservar todos los pesos y logs, incluidos experimentos fallidos.
- La inferencia Vulkan experimental fusionada alcanza paridad de ≤1 nivel respecto a PyTorch, pero cuesta 5,0–5,4 ms a 1080p para el selector C=8; no está integrada. El prototipo de matrices cooperativas FP16 existe, pero una versión FP16 de la red **no** se implementó ni se midió. El presupuesto de aceptación es <1,5 ms GPU a 1080p.

## Problemas abiertos y reanudación

Persisten humo duplicado y bordes dobles en partículas translúcidas, regresiones de desoclusión con cámara móvil y ausencia de una señal de confianza de visibilidad fiable. La corrección de escala de MV por ticks medida en sesión 15 mejora la máscara de partículas en A/B, pero no valida los modelos entrenados antes. La regla regional de profundidad de sesión 16 ganó 0,6–0,9 dB con cámara fija y perdió 8,3 dB en reserva móvil; no integrar. Revisar `NEURAL_GHOST_METRICS.md`, `NEURAL_EVALUATION.md`, `PARTICLE_MV.md`, `VISUAL_QUALITY.md` y el estado sin commit de sesión 16 para los detalles y evidencias.

1. Volver al worktree experimental y respaldar/commitir allí los cambios pendientes de investigación. Verificar montaje Steam y archivos originales; restaurar ROCm/PyTorch si procede.
2. Auditar `session15-new` con `tools/particles/recording_audit.py` y reconstruir datasets separados de entrenamiento/validación a un tick por cuadro; no sustituir los originales. Diagnosticar humo con máscaras alfa/confianza, MV y profundidad reproyectada en ambos extremos.
3. Medir PSNR, SSIM, MAE móvil, gross, mezcla, correlación de duplicados, nitidez, diferencia temporal y coste GPU contra el mismo generador G. Elegir arquitectura, umbral y checkpoint **solo con TRAIN/VALIDATION**, aplicando `NEURAL_ACCEPTANCE.md` v2.
4. TEST3 (`t3_part1`, `t3_part2`, `t3_transp`, `t3_occl`, `t3_fast`, `t3_combo`) está **reservado** para una única evaluación final por familia ya fijada. No abrirlo para selección de modelos, hiperparámetros ni diagnóstico iterativo; hacerlo contaminaría la medida independiente de generalización. El diagnóstico previo del generador con `t3_part1` no autoriza usarlo para escoger la red.

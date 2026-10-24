# Motor de inferencia Vulkan (`native/src/neural_pass.cpp`, `shaders/neural_sel.comp`, `tests/neural_test.cpp`)
Un único dispatch compute fusionado: tile 16x16, ventana de entrada 24x24, las 4 capas en memoria compartida (FP32, pesos en storage buffer, 12,5 KiB para C=8 / 34 KiB para C=16), mezcla de candidatos al final. Pesos exportados con `export_weights.py` (tau horneado). Sin trabajo de CPU por fotograma.
**Paridad numérica** (`neural_test`, RX 9060 XT RADV, fotograma real de disocclusion): máx |GPU − PyTorch| = **1 nivel de 8 bit**, 0 de 2 764 800 valores con diferencia >1 (C=8 y C=16).
**Coste GPU** (mediana de 30 repeticiones, marcas de tiempo; 1080p = extrapolación por nº de píxeles desde 1280x720):
| Modelo | todos los tiles | tiles filtrados (≈60 % ejecutados en la prueba) |
|---|---|---|
| C=16 | 21,7 ms | 12,7 ms |
| C=8 | 4,8 ms | 2,5 ms |
Es un kernel escalar FP32. NO se implementó la versión con matrices cooperativas FP16 (el GEMM de `tools/coopmat` sugiere margen, pero no está medido para esta red). VRAM del motor: pesos <40 KiB + 8 imágenes de entrada del pipeline ya existentes. No integrado en Minecraft (ver NEURAL_EVALUATION).

## Sesión 12 — coste medido a 1080p (sin extrapolar)
`neural_test ... 1920 1080` con muestra remuestreada a 1080p, 3 ejecuciones × 30 repeticiones, RX 9060 XT sin otra carga: kernel fusionado actual (C=8, 6 logits): **5,04–5,39 ms todos los tiles; ≈4,0 ms con el 60 % de los tiles**. El modelo `sbg8` evaluado necesitaría 7 candidatos y la máscara de puerta de 17 px (no implementado en el shader), así que su coste real sería ≥ este. Matrices cooperativas FP16: NO implementadas (canales=8 padded a 16 desperdician 4×; no se justifica antes de tener un modelo aprobado). No integrado en Minecraft.

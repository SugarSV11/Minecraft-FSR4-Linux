# Investigación de IA — capacidades reales de la GPU (medido 2026-10-12)

## Revisión 2026-10-22
Los checkpoints `s14_k3.best.pt` y `s14_k4.best.pt` siguen presentes en `tools/ai/neural/runs/`, pero su validación previa no supera G. No se repite la línea `train3`. El entorno ROCm `~/neuralvenv/v` ya no está disponible en este sandbox y la GPU no es visible (`/dev/dri` ausente). Las dos grabaciones `session15-new/v3_part{1,2}_m_rate1` sí contienen 150 cuadros y los planos A/B de profundidad y movimiento, además de `cam`; la auditoría las marca aptas para replay cuando haya GPU. TEST3 permanece reservado. Una hipótesis concreta para la siguiente fase es clasificar píxeles de humo por cobertura alfa/confianza de profundidad y corregir solo los de alta confianza; requiere validación contra G en MAE, SSIM, bordes dobles y coste GPU antes de cualquier entrenamiento o integración.

Equipo: RX 9060 XT (RADV GFX1200), Mesa 26.2.4, kernel 7.2.8. Sonda: `tools/coopmat_probe.c`
(compilar en el toolbox: `gcc coopmat_probe.c -o cm -lvulkan`). Mesa visto desde el toolbox fedora-toolbox-42.

## Extensiones y características (vulkaninfo)
- `VK_KHR_cooperative_matrix` rev 2: `cooperativeMatrix = true`.
- `VK_EXT_shader_float8`: `shaderFloat8 = true`, `shaderFloat8CooperativeMatrix = true` (declarado).
- `shaderBFloat16Type = true`, `shaderBFloat16DotProduct = true`; `shaderFloat16`, `shaderInt8` true.
- Subgrupo 32/64, memoria compartida de cómputo 64 KiB.

## Formas de matriz cooperativa realmente enumeradas (`vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR`)
Todas con tamaño 16x16x16 y ámbito subgrupo:
- f16·f16 → f16 y f16·f16 → f32.
- int8 (u8/s8 en todas las combinaciones) → int32, con y sin saturación.
- **No aparece ninguna combinación FP8 ni BF16**, aunque la característica `shaderFloat8CooperativeMatrix` esté en true.
  Conclusión: hoy, vía este Mesa, la inferencia FP8 con matrices cooperativas NO es utilizable desde Vulkan; las rutas disponibles son FP16 e INT8.
  (FSR 4 usa FP8 en DX12/WMMA mediante el driver propio de AMD; no es reproducible aquí con RADV.)

## Implicaciones
- Una red de interpolación propia en Vulkan tendría que usar FP16/INT8 WMMA 16x16x16. Es viable técnicamente, pero no existen modelos con licencia utilizables ni datos de entrenamiento en el proyecto; el presupuesto de 0,5 ms por fotograma a 1080p es inferior al coste medido de FG 4.0.1 (1,3–2,0 ms), así que cualquier red debería costar bastante menos que la de AMD.
- Estado: investigación de capacidades hecha; prototipo de red NO iniciado. Ver `docs/FRAME_GENERATION.md` para el límite de 2000 FPS.

## Prototipo FP16 con matrices cooperativas (medido 2026-10-12) — `tools/coopmat/`
GEMM C(f32) = A(f16) · B(f16), `VK_KHR_cooperative_matrix` 16x16x16 (f16·f16→f32, la combinación enumerada). Marcas de tiempo GPU (`vkCmdWriteTimestamp`, 20 repeticiones tras calentamiento, periodo 10 ns), verificado contra una referencia en CPU (doble precisión, entradas ya redondeadas a fp16) en 8 filas: error máx. 2e-6 (M=2073600) y 1.9e-4 (2048³, |ref| hasta 71) — es decir, ejecución real y resultado correcto. Se ejecuta de verdad en la matriz de la GPU: el kernel escalar equivalente tarda entre 2.6x y 10x más.
| Forma | coop 1 tile/wave | coop 2x2 tiles/wave | escalar convencional (1 hilo/salida, sin LDS) |
|---|---|---|---|
| 2048x2048x2048 (17.2 GFLOP, limitado por cómputo) | 2.64 ms · 6.5 TFLOP/s | 2.07 ms · 8.3 TFLOP/s | 21.55 ms · 0.80 TFLOP/s |
| M=2 073 600 (1080p), N=16, K=144 (9.56 GFLOP; "conv 3x3 16→16 canales" como GEMM im2col) | 2.46 ms · 3.9 TFLOP/s (limitado por ancho de banda: A pesa 597 MB) | n/a (N no múltiplo de 32) | 6.45 ms · 1.5 TFLOP/s |
Límites honestos: el kernel de referencia escalar es deliberadamente ingenuo (sin tiling en memoria compartida), así que la ventaja real frente a un shader FP16 bien optimizado sería menor; los kernels coop tampoco están cerca de un pico teórico (no se afirma ninguno). El caso 4096³ con el kernel escalar superó el watchdog del driver (dispositivo perdido) y se descartó.
Modelo de coste (aritmética sobre lo medido, NO una red medida): una sola capa conv 3x3 16→16 a 1080p cuesta ≥2.5 ms con este kernel (como GEMM im2col), ya 5x por encima del presupuesto de 0.5 ms por fotograma que exigiría 2000 FPS. Una red de interpolación aprendida a resolución completa no cabe; a media resolución y 8 canales sería ≈1.2 GFLOP/capa (≈0.15 ms a 8 TFLOP/s si fuera limitada por cómputo), pero no existen pesos entrenados ni licencia/datos en el proyecto, por lo que NO se implementó ninguna red de reconstrucción. No se sustituye FSR 4.1.1 ni AMD FG.

## Sesión 10: ¿puede una red pequeña mejorar los fotogramas generados? (experimento reproducible, resultado negativo)
Código: `tools/ai/refine_experiment.py`, `tools/ai/error_budget.py`; logs/JSON en `tools/ai/`. Datos: las 7 escenas grabadas de la sesión 9 (1280x720, fotogramas reales c0/c1, verdad en t≈1/3 y 2/3 y salida del shader), **escenas de entrenamiento y de prueba disjuntas** (entrena: baseline, vegetación, agua, animales; prueba: desoclusión, transparencia, partículas; solo fase con cámara móvil).
Red residual `out = G + conv(relu(conv(relu(conv([G, c0, c1, t])))))`, CPU/PyTorch, FP32 (sin cuantizar; no se midió inferencia en GPU de esta red).
| Red | Pesos | GFLOP por fotograma 1080p | Pérdida | Ganancia PSNR en escenas de prueba |
|---|---|---|---|---|
| 3 capas, 8 canales | 1 531 | 6,3 | L1 / MSE | +0,00 dB (SSIM +0,000…+0,005) |
| 6 capas dilatadas, 24 canales ("oráculo" sin límite de coste) | 23 667 | 97,6 | MSE | −0,00…+0,00 dB (SSIM −0,001…+0,004) |
Coste de referencia medido en esta GPU (`tools/coopmat`): GEMM FP16 con matrices cooperativas 3,9 TFLOP/s (limitado por ancho de banda) frente a 1,5 TFLOP/s escalar; la red de 6,3 GFLOP costaría ≥1,6 ms por fotograma generado (modelo, no implementado) frente a 0,34 ms del shader completo de generación.
**Por qué:** `error_budget.py` muestra que 72–99 % del error está en 0,5–8,5 % de los píxeles (movimiento, desoclusión, partículas) —información que un filtro local de 7x7 no tiene—; un denoiser perfecto daría como mucho +0,05…+1,5 dB. **Conclusión:** la IA de refinado no supera al enfoque tradicional a un coste razonable; se deja como experimento aislado, desactivado, sin integración. Límites honestos: entrenamiento corto en CPU (≤30 min), dataset pequeño, una sola arquitectura; no se descarta que una red con vectores de movimiento mejores o con entrada de máscaras de oclusión rinda más, no probado. Ruta recomendada en su lugar: mejores vectores (partículas).
FP8/BF16 con matrices cooperativas: sin cambios (sonda de la sesión 7; no re-ejecutada en esta sesión).


## Sesión 11: corrector neuronal por selección (resultado: motor y paridad OK, calidad NO aprobada)
Detalle en `NEURAL_DATASET.md`, `NEURAL_ARCHITECTURE.md`, `NEURAL_TRAINING.md`, `NEURAL_INFERENCE.md`, `NEURAL_EVALUATION.md`. Red de 3 417 pesos (selecciona entre 6 candidatos): +0,74 dB en VAL, pero en TEST −1,06 dB (disocclusion), −0,17 (fastentity), −0,09 (transparency), +0,79 (particles). Motor Vulkan fusionado con paridad ≤1 nivel y 4,8 ms (1080p, todos los tiles). No integrado, desactivado, sin controles de menú.

## Sesión 12: segunda fase (dataset ×3, ROCm, regla tradicional, 8 familias)
Ver `NEURAL_*.md` y `NEURAL_ACCEPTANCE.md`. Resultados: (1) la regla tradicional "hueco→mezcla temporal" da +0,81 dB en TEST2 sin parámetros ni coste; (2) el mejor selector aprendido (`sbg8`, 3 593 pesos, entrenado en ROCm) da +0,96 dB pero regresa en una escena de TEST2, empeora partículas (humo duplicado) y no cumple los criterios preregistrados; (3) historia temporal no es el cuello de botella; (4) coste 5,0–5,4 ms a 1080p medido. Decisión: NO integrar; mantener en rama experimental.

## Sesión 13 (pausada)
PSNR engaña: la red `sbg8` ganaba +0,5…+1 dB promediando imágenes (más MAE, más imagen doble). Nuevas métricas de duplicación/desenfoque/temporales con controles negativos (`NEURAL_GHOST_METRICS.md`). Vectores de partículas actuales: empeoran el generador dentro de las partículas (A/B). Selección dura sin mezclas: no mejora aún. Ver `NEURAL_EVALUATION.md`.

## Sesión 15
No se entrenó ningún modelo nuevo. Hallazgo que afecta a la IA: los datos de partículas anteriores se grabaron a 12 ticks/fotograma y con vectores de partícula mal escalados (ver `docs/PARTICLE_MV.md`). Antes de reentrenar hay que regrabar con `TICKRATE=1`; la línea de selección dura (k1–k4) sigue descartada.

## Reanudación 2026-10-22: puerta para una red de desoclusión

El análisis completo de 49 casos de desoclusión localiza 47,36 % del MAE RGB en una banda circular de profundidad que ocupa 17,70 % de la imagen (definición del nuevo script). Un corrector no neuronal limitado a esa banda se implementó en `tools/disocclusion/edge_corrector.py` y se midió en escenas con cámara fija y móvil. El reemplazo por el fotograma temporalmente más cercano ganó 0,6–0,9 dB en cámara fija, pero perdió 8,3 dB en la reserva móvil. La regla conservadora de acuerdo de color perdió 0,41 dB en la reserva móvil; al exigir además desacuerdo de profundidad quedó prácticamente neutra (−0,005 dB), sin ganancia. Aumentó también el MAE en bordes y el ghost proxy en las reglas no triviales. Ver `docs/VISUAL_QUALITY.md` y `docs/evidence/session16/edge_corrector.json`.

Por ello no se entrenó una red pequeña ni se abrió TEST3: todavía no hay una señal local fiable que distinga el color correcto con cámara móvil, y entrenar sobre una sustitución que ya regresa no justifica coste GPU ni selección de hiperparámetros. La hipótesis pendiente es usar correspondencias de MV y visibilidad por profundidad reproyectada, con máscaras de confianza derivadas de ambos extremos, antes de volver a evaluar una red regional. TEST3 permanece sellado conforme a `NEURAL_ACCEPTANCE.md`.

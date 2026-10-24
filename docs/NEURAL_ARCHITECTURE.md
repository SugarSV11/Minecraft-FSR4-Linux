# Arquitecturas evaluadas (`tools/ai/neural/models.py`)
Todo son convoluciones 3x3 + ReLU (mapea directo a Vulkan compute).
- A `ResCNN` (residual compacta: out = G + f(x)); B `TinyUNet` (2 niveles); C/D `Selector`/`selres`: red guiada por vectores/profundidad/máscaras que predice pesos softmax sobre seis candidatos {G, A, B, C0, C1, mezcla} (no genera píxeles nuevos: elige/mezcla los existentes, de modo que no puede inventar ni emborronar fuera de ellos).
- `selres8` (elegido): 4 capas 3x3: 22→8→8→8→6, **3 417 parámetros**, 7,0 GMAC a 1080p con todos los píxeles. `selres16`: 22→16→16→16→6 (8 694 pesos).
- Procesado selectivo: `flag_mask` + `tile_flag` (tiles 16x16 + vecinos); en el motor Vulkan los tiles sin marca copian G.
- E (corrector de desoclusión) queda cubierto por el canal "hueco rellenado" de entrada; F (historial temporal largo) NO se probó.

## Sesión 12 — familias comparadas (`models.py`)
Todas parten de la regla R = generador con los píxeles "hueco rellenado" sustituidos por la mezcla temporal (1−t)·C0 + t·C1 (técnica tradicional, 0 parámetros). Se retira la cabeza residual (tanh) de la sesión 11 que el shader no ejecutaba.
- `sb8` / `sb16` (A, selector CNN 22→c→c→c→7, candidatos {R, G, A, B, C0, C1, mezcla}), `sbctx8` (D, + rama de contexto a 1/4 de resolución), `sbunet12` (C, U-Net), `resb16` (B, R + residual), `sbg8` (F, selector con puerta: fuera de la máscara de sospecha dilatada 17 px la salida ES R), `sbn8/sbng8` (sin candidato de mezcla: solo píxeles reales), `selres8` de la sesión 11 (A original).
- E (historia temporal): descartada con evidencia (oráculo +1,5…+3 dB, el cuello de botella es el discriminador), no entrenada.
Modelo final evaluado: **`sbg8` = 3 593 parámetros** (22→8→8→8→7 + puerta de máscara).

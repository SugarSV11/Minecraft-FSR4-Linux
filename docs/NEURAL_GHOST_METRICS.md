# Métricas de ghosting (sesión 13)

Código: `tools/ai/neural/ghostmetrics.py` (métricas), `ghostcontrols.py` (controles negativos), `ghost_eval.py` (evaluación de G / mezcla temporal B / regla R / checkpoints).
Todas se calculan contra el fotograma intermedio VERDADERO y son deterministas (sin red perceptual aprendida: no hay pesos offline; se dice explícitamente que no se midió LPIPS/FLIP).
Las definiciones se congelaron ANTES de aplicarlas a ningún corrector y no se tocaron después.

| Métrica | Qué detecta |
|---|---|
| `excess_f/c` | energía de bordes del resultado SIN contrapartida (±2 / ±6 px) en la verdad: bordes dobles, puffs duplicados (fino σ=1; grueso σ=3 para humo de bajo contraste) |
| `missing_f/c`, `hf_ratio`, `hf_corr` | bordes que faltan, energía y correlación de alta frecuencia: desenfoque y pérdida de detalle |
| `dupcorr`, `dupcorr_mov` | mayor correlación POSITIVA del residuo (salida−verdad) con una copia desplazada (6–32 px, 8 direcciones) de la estructura fina de la verdad: ghost = el contenido de la verdad sumado otra vez con desfase |
| `mix%` | % de imagen donde la salida es una MEZCLA temporal de C0 y C1 (±4 niveles de w·C0+(1−w)·C1, w∈{.2…8}) y la verdad no lo es: imagen doble tipo blend |
| `stale%` | % donde la salida copia UN fotograma real y la verdad no |
| `tdiff`, `flick` | consistencia temporal de la secuencia [C0,g1..gn,C1]: error del cambio temporal respecto al de la verdad, y brusquedad relativa |
| `mae`, `mae_mov`, `gross_mov%` | error absoluto medio global / en la región móvil (|C1−C0|≥12 dilatada) / % de píxeles con error ≥16 en la región móvil |
| `hole_mae`, `hole_gross%` | error dentro de los píxeles rellenados por el generador (desoclusión) |
| `ms_ssim`, `gsim` | estructura multiescala y similitud de gradientes |

## Controles negativos (`ghostcontrols.py`, 3 escenas, 8 fotogramas cada una; `docs/evidence/neural/ghost_controls.txt`)
Fotogramas verdaderos degradados a propósito. Ghost, desenfoque y ruido se calibraron para tener EXACTAMENTE el mismo PSNR (29,04 dB): PSNR no puede distinguirlos; las métricas sí:
| control | PSNR | firma que lo detecta | valores |
|---|---|---|---|
| perfecto | ∞ | todo en su valor neutro | 0 / 1 |
| ruido | 29,04 | `excess_f` 0,20 alto, `hf_ratio` 1,93, `dupcorr` 0,004 | no es ghost |
| desenfoque σ1,5 | 29,04 | `missing_f` 0,31, `hf_ratio` 0,28 | `dupcorr` 0,12, `mix%` 1,6 |
| **ghost** (35 % de copia a 8 px) | 29,04 | **`dupcorr` 0,256** (el mayor), `missing_c` | `excess_f` 0,055 |
| blend temporal | 25,5 | **`mix%` 4,37** (≥2,7× el siguiente), `flick` 0,35 | |
| copia obsoleta (C0) | 24,1 | **`stale%` 5,52** | |
| desplazamiento 3 px | 23,1 | `hf_corr` 0,04, `stale%` 1,3, `dupcorr` 0,23 (se confunde parcialmente con ghost: es un desplazamiento) | |
| flicker ±6 % | – | `tdiff` 30 (solo la secuencia lo ve) | |
| hueco mal rellenado | 40,6 | PSNR casi no se mueve; `hole_mae` 7,9 sí | |
Límites honestos: `dupcorr` también sube con desplazamientos globales; `excess_f` se activa con ruido; `mix%` solo ve mezclas de los dos fotogramas reales (no duplicaciones por partículas con movimiento propio, para esas sirve `dupcorr`).

## Aplicación a resultados históricos (G, regla R y la red `s12_h1`; 17 escenas, 12 fotogramas/escena; `ghost_metrics_h1.txt/json`)
Diferencia respecto al generador G (media de 17 escenas):
| | PSNR | MAE | MAE móvil | gross móvil (pts) | mix% | hf_ratio | hole_mae |
|---|---|---|---|---|---|---|---|
| R (huecos→mezcla) | +0,34 | −0,016 | −0,036 | +0,18 | +0,26 | −0,014 | **+1,59** |
| s12_h1 (sbg8) | +0,47 | **+0,10** | **+0,36** | **+1,37** | **+0,69** | −0,073 | +1,53 |
**Hallazgo:** la ganancia de PSNR de las redes es una ganancia de MSE (se reducen unos pocos errores enormes promediando dos imágenes). Con métricas de diagnóstico, `s12_h1` empeora el error absoluto, el error grosero en movimiento (+5,8 pts en part_d, +8,2 en particles, +5,2 en mixed), la mezcla de dos imágenes y la nitidez, aun cuando su PSNR sube +0,5…+0,8 dB. La regla R también empeora el relleno de huecos (`hole_mae` +1,6 en media): el relleno del generador es mejor que mezclar. Ninguno de los dos mejora visualmente.

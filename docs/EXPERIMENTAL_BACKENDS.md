# Backends y prototipos experimentales (sesión 7)

Ninguno está integrado en el juego ni es un backend seleccionable. El mod sigue exponiendo solo NATIVE, FSR_3_1_5, FSR_4_1_1 y AMD_FG_4_0_1 (los dos de FSR y FG ejecutados por el runtime oficial de AMD).

## Interruptores de experimento (variables de entorno, desactivados por defecto)
- `FSR4_GPU_TS=1`: marcas de tiempo GPU en los command buffers del puente (solo medición; sin efecto visual).
- `FSR4_QTYPE=compute`: el helper usa una cola D3D12 COMPUTE. Medido: sin mejora.
- `FSR4_NOWAIT=1`: la GPU no espera la salida del upscaler. **Muestra imágenes incorrectas; solo para medir solapamiento.** No usar jugando.

## FG ×3 — prototipo offline por reproyección (NO neuronal, NO integrado): `scripts/fgx3_proto.py`
El proveedor oficial AMD FG 4.0.1 genera un único fotograma intermedio (×2); no hay un modo ×3/×4 en su API usada aquí, así que no se intentó forzarlo. Como estudio de viabilidad se implementó un generador clásico para el caso de rotación pura de cámara: homografía `K·R·K⁻¹` (exacta para una cámara que gira, a cualquier profundidad) aplicada a dos fotogramas reales F0 y F3 y mezcla ponderada por tiempo para obtener t=1/3 y t=2/3.
Evaluación sin trampas: la verdad temporal son fotogramas nativos reales (la fase de giro de `scripts/motion-test.sh` renderiza cada 0,3° de yaw de forma determinista), la focal se calibra en otros pares (k,k+1), 38 fotogramas sintetizados, región sin HUD/chat:
| Método | PSNR | SSIM |
|---|---|---|
| Duplicar el fotograma real más cercano (NO es generación) | 23.40 dB | 0.7653 |
| Mezcla 50/50 ponderada sin movimiento | 24.71 dB | 0.7540 |
| **Reproyección por rotación (prototipo)** | **33.47 dB** (mín. 32.32) | **0.9673** |
Alcance real: solo rotación de cámara. No maneja traslación (necesitaría profundidad y desoclusiones), entidades que se mueven por sí solas, partículas, HUD (se excluyó de la medida) ni latencia (generar ×3 exige conocer F3 antes de mostrar t=1/3: +1 fotograma de retraso respecto al real, como FG ×2). No se midió el coste GPU de la pasada. **No se integra**: sin verificación de calidad con traslación/entidades, estabilidad ni latencia en el juego, y con las mediciones de FG (§4 de PERFORMANCE_OPTIMIZATIONS.md) cada fotograma presentado extra no compensa el trabajo restante frente al nativo a 1080p.

## GEMM FP16 con matrices cooperativas — `tools/coopmat/`
Ver `docs/AI_RESEARCH.md` (correcto frente a CPU, ejecutado en la GPU con marcas de tiempo, 2.6x–10x más rápido que un shader escalar ingenuo). Es una demostración técnica de cómputo, no un reconstructor: no hay pesos entrenados, y no reemplaza ni compite con FSR 4.1.1.

# FG ×4 propio (EXPERIMENTAL, sesión 10, 2026-10-13)

**Qué es.** Mismo generador que ×3 (`shaders/fgx3.comp`, `FgX3Pass::recordGenerateN`), con tres fotogramas generados por fotograma renderizado en t = 0,25 / 0,50 / 0,75 entre el real anterior y el actual, más el real: 4 imágenes presentadas. Backend propio, separado del proveedor AMD (que solo ofrece ×2). No duplica imágenes: si no hay fotograma previo válido o no hay imagen de swapchain libre, se presentan menos. Activación: `"fgMultiplier": 4` en `config/fsr4linux.json` (el menú F6 sigue alternando ×2/×3; ×4 no está expuesto). Requiere la presentación por lotes (`docs/PRESENTATION_ARCHITECTURE.md`) para ser útil; en VSync usa la ruta por imagen.

**Qué se hizo:** `recordGenerateN(idx, n)` (t = idx/n), `fsr4_x3_record_generate_n`, tres destinos de generación, anillo de 16 semáforos de adquisición, `maxExtra = n-1`.

**Calidad (en juego, contra fotogramas verdaderos de una ejecución densa a 1/4 de paso, `x3-ingame-report.py A B 4`):**
| | Giro (ROI sin HUD) PSNR / SSIM / error grosero | Oveja PSNR / SSIM |
|---|---|---|
| ×4 generado (75 fotogramas giro, 72 oveja) | **34,07 dB / 0,964 / 0,16 %** | **29,74 dB / 0,981** |
| mezcla temporal | 28,74 / 0,875 / 1,11 % | 28,04 / 0,953 |
| duplicar el más cercano | 27,21 / 0,860 / 1,73 % | 26,73 / 0,952 |
Cada fotograma generado está más cerca de un real que en ×3 (t máximo 0,25 de distancia), pero el modelo es el mismo (proyección con vectores, sin huecos rellenados por inpainting) y los límites son los de ×3: partículas, agua en flujo, translúcidos y huesos. **×4 offline en las 7 escenas** (`x3_scene_dataset.py ... 4` sobre las grabaciones de la sesión 9, reproducción por el paso real, `docs/evidence/session10/x4_scene_eval.txt`; PSNR generado / mezcla en dB): cámara móvil: baseline 35,3/27,7 · agua 36,7/28,8 · vegetación 33,7/25,7 · transparencia 35,7/28,1 · desoclusión 33,9/24,8 · animales 30,0/26,2 · partículas 21,6/21,4 (+7…+9 dB sobre mezclar salvo partículas); cámara fija ≈ mezcla (−0,1…−2,5 dB; partículas −0,7), igual que ×3. Error grosero con cámara móvil 0,16–0,89 % (partículas 6,3 %).

**Rendimiento (1080p, VSync OFF, 2 sesiones × 3 ventanas; ronda 1 con el monitor principal, ronda 2 con el vertical, sin diferencias de método):** FG ×4 solo: 465/446 renderizados, 1396/1338 generados, **1862/1784 presentados/s** (ronda 2: **1865 / 1887**, `docs/evidence/session10/suite2_table.md`), GPU 99 %; FSR 4.1.1 + ×4: 380/372 renderizados, 1519/1490 presentados. Nativo ≈1200. Coste GPU por fotograma generado ≈0,34 ms (×3) → ×4 gasta ≈1,0 ms de generación por fotograma renderizado. **No se alcanza 2000/s.** El techo aritmético: cada fotograma renderizado cuesta ≈0,86 ms de mundo + 3×0,34 ms + blits ≈2,2 ms de GPU.

**VSync 100 Hz:** 25 renderizados, 100 presentados, cola present→visible 33,7 ms (ruta por imagen). Con VSync ×4 no tiene sentido práctico (el juego baja a 25 FPS).

**Veredicto.** ×4 funciona, es estable bajo validación Vulkan y mantiene la calidad de ×3, pero solo sube un 13 % las presentaciones frente a ×3 (que ya satura la GPU) y añade más extrapolación temporal; **×3 sigue siendo la mejor opción experimental**; ×4 queda como modo opcional. La variante de shader de 64 bits (una pasada) no cambió el resultado medido en juego; 2000/s sigue sin alcanzarse.

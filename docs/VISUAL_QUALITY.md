# Calidad visual: jitter, vectores de movimiento, profundidad (sesión 5)

## Sesión 9 (2026-10-12): calidad de FG ×3 en escenas variadas y con FSR 4.1.1
**Método.** `scripts/x3-scene.sh ESCENA DIR` graba una secuencia densa (cada fotograma renderizado, HUD oculto, el mundo SIGUE avanzando: partículas, agua y animales se mueven) con color, planos de profundidad/vectores y marca de tiempo por fotograma; `scripts/x3_scene_dataset.py` toma como reales los fotogramas k y k+3 y como **verdad** los k+1 y k+2 de la misma ejecución, calcula t real con las marcas de tiempo, y `build/cmake/fgx3_replay` pasa los datos por el **pase real del juego** (`native/src/fgx3_pass.cpp` + `shaders/fgx3.comp`); `scripts/x3_scene_eval.py` mide PSNR/SSIM, PSNR sobre los píxeles que cambian, píxeles con error grosero (>40 niveles) y flicker de la secuencia presentada [real, g1, g2, real] frente a la verdadera. Fase A = cámara fija (solo se mueve la escena), fase B = cámara en movimiento (giro 0,15°/0,1° por fotograma o desplazamiento lateral 0,04 bloques/fotograma). 49 casos × 2 fotogramas por escena, 1280x720, vectores de entidad activos. Datos: `docs/evidence/session9/scene_eval_final.txt`.
| Escena | Fase A (cámara fija) PSNR generado / mezcla | SSIM | Fase B (cámara móvil) PSNR generado / mezcla | SSIM | Error grosero B (gen / mezcla) |
|---|---|---|---|---|---|
| base (hojas, piedra, cristal) | 54,2 / 54,1 | 0,999 | **36,0 / 28,4** | 0,969 | 0,14 % / 1,2 % |
| partículas (hogueras, nubes de humo/llama) | 22,1 / 22,8 | 0,884 | 21,8 / 21,6 | 0,852 | 6,1 % / 6,5 % |
| agua animada | 55,9 / 56,1 | 0,999 | **37,4 / 29,7** | 0,983 | 0,12 % / 0,8 % |
| vegetación (hierba, helechos, vides, hojas) | 65,1 / 66,6 | 1,000 | **34,3 / 26,5** | 0,967 | 0,26 % / 1,8 % |
| transparencias (vidrios de colores, hielo, miel, slime, agua, oveja detrás) | 52,7 / 54,0 | 0,999 | **36,4 / 29,0** | 0,972 | 0,14 % / 1,0 % |
| desoclusión (columna delante de muro, desplazamiento lateral) | 57,7 / 60,1 | 1,000 | **34,9 / 25,4** | 0,974 | 0,24 % / 2,7 % |
| animales con IA (caballo, golem, aldeano, cerdo, vaca, lobo, gallina) | 37,6 / 39,0 | 0,990 | **31,0 / 27,0** | 0,962 | 0,72 % / 1,7 % |
- Con la cámara en movimiento el generado supera a la mezcla temporal en **+7 a +10 dB** (+4 dB con animales de IA) y el flicker de la secuencia presentada queda en x0,92–0,99 de la verdadera (duplicar/mezclar: x0,6).
- **Cámara fija: el generado no mejora a la mezcla** (−0,1 a −1,5 dB, siempre >37 dB salvo partículas) — con poca diferencia entre fotogramas no hay nada que compensar y el remuestreo añade un error pequeño.
- **Partículas: límite medido.** Sin vectores propios (las partículas no escriben profundidad ni vectores) el generado equivale a la mezcla (22 dB, 6 % de píxeles groseramente erróneos tanto en generado como en mezcla). Probé estimar movimiento por coincidencia de bloques sobre el color (16x16, ±16 px): MSE 444 vs 445 de la mezcla, sin ganancia; el movimiento de las partículas entre fotogramas es demasiado grande y aleatorio. No se implementó: sería coste sin mejora demostrada. La vía correcta serían vectores exactos por partícula desde el motor (posición previa/actual), no hecha.
- **Cambios del sombreador justificados por las medidas** (`FSR4_X3_VARIANT`): en píxeles donde la profundidad previa no concuerda, antes se usaba siempre el fotograma actual F1; ahora se usa el temporalmente más cercano **salvo que F0 esté ocluido por algo más cercano** (variante 2). Efecto: cámara fija +0,5…+1,5 dB en todas las escenas y la desoclusión intacta (34,97 vs 34,96; la variante 1, "siempre el más cercano", la empeoraba a 33,6). **UI translúcida**: el HUD oscuro translúcido (chat, fondo de la barra) se recompone ahora de forma multiplicativa (`final/mundo` aplicado al mundo generado): 53,1 dB frente a 44,3/48,9 dB con el método aditivo anterior (`tests/fgx3_test`, caso nuevo; umbral 50 dB).
**FSR 4.1.1 Quality + FG ×3 contra referencias temporales reales** (juego completo, escena determinista de `motion-test.sh`, 1280x720, FG ×3 frente a una ejecución al tercio de paso; `docs/evidence/session9/ingame_fsr_x3.txt`):
| Referencia | Fase | Generado PSNR / SSIM | Duplicar | Mezcla | Error grosero (gen / dup / mezcla) | Flicker |
|---|---|---|---|---|---|---|
| FSR 4.1.1 verdadero (error puro de FG) | giro de cámara | **35,85 dB / 0,967** | 28,48 / 0,885 | 30,08 / 0,894 | 0,04 % / 0,71 % / 0,59 % | x0,88 |
| FSR 4.1.1 verdadero | oveja | **29,60 dB / 0,975** | 27,26 | 28,51 | 0,80 % / 1,44 % / 1,40 % | x0,70 |
| Nativo verdadero (incluye la pérdida del reescalado) | giro | 31,45 dB / 0,939 | 27,12 | 28,21 | 0,29 % / 1,24 % / 0,97 % | x0,80 |
| Nativo verdadero | oveja | 25,28 dB / 0,904 | 24,18 | 24,77 | 2,20 % / 2,75 % / 2,88 % | x0,35 |
| Render nativo + FG ×3 (sin FSR) vs nativo verdadero | giro | 31,70 dB / 0,950 | 26,34 | 27,59 | 0,29 % / 1,87 % / 1,29 % | x0,86 |
La ganancia del generador es de +5,8 dB sobre la mezcla en giro y +1,1 dB en la oveja; las cifras absolutas frente al nativo quedan limitadas por FSR (≈4,4 dB menos que frente a FSR). Ojo: las cifras absolutas de esta tabla NO son comparables con las de la sesión 8 (el mundo de pruebas contiene más geometría tras las escenas nuevas); sí lo son entre filas de esta tabla. Ghosting: el error grosero del generado es 4–18 veces menor que el de duplicar/mezclar en giro.


## Sesión 8 (2026-10-12): vectores de movimiento (orientación Y, carrera en el pase de preparación, vectores por entidad)
Todas las cifras de calidad de esta sección comparan contra **fotogramas verdaderos** (el mismo estado determinista del juego renderizado de forma nativa), salvo que se diga lo contrario. Datos: `docs/evidence/session8/`; fotogramas en `~/Escritorio/FSR4-evidence-session8` (fuera del repo por tamaño, ~15 GB).
**1. Orientación Y (corrección real).** Los render targets de Minecraft Vulkan están almacenados de abajo arriba (fila 0 = parte inferior): reconstruyendo una entidad conocida desde el plano de profundidad con un volcado síncrono, el signo antiguo `-ndc.y` colocaba la oveja a rel.y=+1,1 en vez de -2,6 (con `+ndc.y` coincide). Giros de yaw puro no lo notan; **pitch y traslación sí**. Corregido en `shaders/fsr4_prep.comp` (`FSR4_MV_LEGACY_Y=1` restaura lo antiguo solo para medir).
**2. Carrera de lectura/escritura en el pase de preparación (error preexistente, corregido).** El pase lee el buffer de profundidad de Minecraft y Minecraft lo limpia justo después (antes de dibujar la mano) en la MISMA submission: sin dependencia de ejecución el borrado podía solaparse con la lectura y los planos de profundidad/MV salían parcialmente a cero (tableros de 64x64 píxeles; una frontera de submit lo ocultaba). Añadida una barrera final en `PrepPass::recordWorld` (coste medido: ninguno, 221–236 vs 217–229 FPS; `FSR4_NO_PREP_BARRIER=1` restaura lo antiguo para medir). Este error invalidó las comparaciones anteriores de esta sesión (que se repitieron).
**3. Resultados tras ambas correcciones** (giro diagonal yaw 0,3° + pitch 0,2°/fotograma y oveja a 0,06 bloques/fotograma, 1280x720 Quality, 2 repeticiones; `mv_ab_report_after_race_fix.txt`). A = Y espejada (antes), B = Y corregida, C = B + vectores por entidad:
| | Giro: PSNR vs nativo | Giro: SSIM | flicker | Estela de la oveja (píxeles fantasma) | PSNR zona de la oveja |
|---|---|---|---|---|---|
| FSR 3.1.5 A | 25,88 / 25,86 | 0,888 / 0,886 | 0,76 | 12,9 % / 12,9 % | 28,2 / 27,8 |
| FSR 3.1.5 B | 26,95 / 26,97 | 0,944 / 0,942 | 0,89 | 13,0 % / 13,3 % | 28,1 / 27,8 |
| **FSR 3.1.5 C** | 27,01 / 27,03 | 0,943 / 0,943 | 0,89 | **5,3 % / 5,0 %** | **29,4 / 29,3** |
| FSR 4.1.1 A | 26,91 / 26,91 | 0,933 | 0,85 | 2,6 % / 2,7 % | 29,5 / 30,0 |
| FSR 4.1.1 B | 27,22 / 27,28 | 0,946 / 0,947 | 0,98 | 2,8 % / 2,5 % | 29,5 / 29,9 |
| FSR 4.1.1 C | 27,27 / 27,26 | 0,947 | 0,98 | 2,3 % / 2,3 % | 29,6 / 30,1 |
- Corrección de Y: **+1,1 dB y SSIM +0,056 (3.1.5), +0,3 dB y SSIM +0,014 (4.1.1)** en el giro; el flicker de 4.1.1 pasa de 0,85 a 0,98 (≈ nativo).
- **Vectores por entidad: la estela de FSR 3.1.5 baja de 13 % a 5 % (−60 %)**; en FSR 4.1.1 de 2,65 % a 2,3 % (−14 %), la zona de la oveja +1,4 dB en 3.1.5 y sin cambio en 4.1.1. Sin regresión en el giro. **Activados por defecto** (`FSR4_ENTITY_MV=0` los desactiva). (Una primera medición "sin ganancia" quedó invalidada por la carrera del punto 2 y por un movimiento de oveja no lineal del arnés, ya corregidos.)
**4. Frame Generation ×2 AMD 4.0.1 contra el fotograma intermedio VERDADERO** (`scripts/x2-ingame-report.py`: ejecución con FG y ejecución al mitad de paso sin FG): giro con escala Y del vector = +1 → **33,7 dB / SSIM 0,966** (mezcla 27,8, duplicar 25,2); con escala −1 → 28,7 dB. Zona de la oveja con vectores por entidad: 32,0 dB (SSIM 0,985) frente a 28,3 dB sin ellos (mezcla 28,2). **RETRACTADO**: una medición anterior de esta sesión contra un proxy (media de los dos reales vecinos, que NO es la verdad para movimiento) concluyó que FG necesitaba la Y invertida; era un artefacto del proxy. Escala Y de FG = +1 (valor por defecto, `FSR4_FG_MVY`).
**Vectores por entidad — qué son y qué no.** `EntityMotion.java` guarda la transformación interpolada (posición + guiñada del cuerpo) de cada entidad del fotograma renderizado anterior y entrega a la GPU las 48 más cercanas (pivote, Δguiñada, caja, desplazamiento); el shader da a cada píxel cuya posición reconstruida cae dentro de la caja el movimiento **rígido** de esa entidad. Historial: se descarta al cambiar de nivel/dimensión y tras un reinicio del temporal; las entidades nuevas no tienen movimiento; un salto >6 bloques cuenta como teletransporte; el jugador en primera persona y las invisibles se omiten. **Aproximaciones declaradas**: (1) rígido, SIN animación de huesos (patas/cabeza/cola); (2) clasificación por caja delimitadora inflada 0,3 bloques (los modelos sobresalen de la caja lógica: 0,04 dejaba bordes sin vector; 0,3 = +3,5 dB en el prototipo ×3); un píxel de fondo dentro de la caja se arrastra con la entidad; (3) máx. 48 entidades; (4) objetos translúcidos sin profundidad no se clasifican. Verificación: `tests/prep_test` (GPU vs referencia CPU en doble precisión, error ≤0,03 px, incluye caja que no impacta) y volcado síncrono en juego (oveja a +5,25 px/fotograma).

Metodología (reproducible, `scripts/vq-compare.py`, harness `sweep/shotat/move/jflip/mvscale/dump`): cámara determinista **por fotograma** (yaw = yaw0 + ω·f, sin interpolación), mundo congelado (`/tick freeze`, hora 12:00, tiempo despejado), captura en el mismo índice de fotograma; cada modo en su propia sesión o, en una sola sesión, solo con giros (el control de posición del servidor congelado hace NO comparables las traslaciones dentro de una sesión). Métrica: PSNR frente al render nativo en la zona de mundo (excluye chat, HUD y mano) + mejor desplazamiento entero ±2 px (un desplazamiento ≠ (0,0) delata desalineación sistemática) + nitidez (energía de gradiente relativa al nativo).

## Defectos encontrados y corregidos
| # | Defecto | Cómo se detectó | Corrección | Efecto medido |
|---|---|---|---|---|
| 1 | **Signo del jitter Y** enviado a FSR opuesto al aplicado a la proyección | 16 combinaciones de signo (proyección X/Y × valor pasado X/Y), escena estática convergida vs nativo | el jitter Y pasado a FFX (y a FG) se niega (`FsrRuntime.jitterFlip = 8`) | 4.1.1 estático 27.9 → **30.6 dB**, nitidez 0.678 → 0.724; giro rápido 27.9 → 31.5 dB y desaparece el desfase de 1 px vertical; 3.1.5 estático 28.2 → 30.2 dB |
| 2 | **Profundidad inservible**: `GameRenderer.render3dHud` limpia el depth buffer (0.0, reversed-Z) antes de dibujar la mano, y la pasada de preparación lo leía después → solo la mano (3.5 %) tenía profundidad | volcado de depuración del plano compartido (`fsr4_debug_dump`) | nueva pasada "de mundo" (profundidad + MV) enganchada por mixin justo ANTES de `render3dHud`; pasada "de mano" después (MV=0 y profundidad de la mano) + copia del color | píxeles con profundidad 3.5 % → **96.8–98.7 %** (el resto es cielo); los MV por traslación dejan de usar profundidad cero |
| 3 | Posible carrera: el buffer uniforme de la pasada de preparación se reescribía por CPU mientras un fotograma anterior en vuelo aún podía leerlo (con la sincronización por fence ya no hay espera de CPU) | revisión de la ruta asíncrona | anillo de 16 ranuras con offset dinámico | sin cambios visibles; elimina un riesgo de parpadeo intermitente |

## Vectores de movimiento: validación
- Rotación: 0.1°/frame → media 1.31 px, 0.5°/frame → 6.34–6.57 px (render 1280x720), razón 5:1; máximo 11.98 px en el borde ≈ f·Δθ/cos²(51°) esperado; signo = convención FSR (previo − actual: giro a la derecha → contenido a la izquierda → MV +x).
- Control negativo (4.1.1, giro lento 0.1°/frame, nitidez): MV correctos 0.691; MV a cero 0.688; signo invertido 0.589; escala ×3 0.577 → los MV erróneos **degradan** la nitidez, los correctos no. El PSNR frente al nativo es casi insensible (±0.6 dB) a los MV en esta escena estática congelada: FSR 4.1.1 se apoya sobre todo en su modelo y el historial; **no se puede afirmar una ganancia grande atribuible a los MV** con esta métrica.
- Los MV son solo de cámara (reproyección con profundidad): entidades, partículas y texturas animadas (agua) NO tienen vectores propios.

## Resultados actuales (1080p, Quality = 1280x720 interna, PSNR vs nativo en zona de mundo)
| Caso | FSR 3.1.5 | FSR 4.1.1 |
|---|---|---|
| estático | 30.20 dB (nitidez 0.753) | 30.61 dB (0.723) |
| giro lento 0.1°/frame | 27.37 (0.538) | 28.73 (0.691) |
| giro rápido 0.5°/frame (~7 px/frame) | 29.89 (0.729) | 31.53 (0.703) |
| traslación 0.03 bloques/frame | 26.56 (0.599) | 28.36 (0.718) |
| escena con agua + vidrio tintado + hojas + cielo y nubes (misma sesión, estático) | 40.48 dB (1.001) | 40.91 dB (0.896) |
En todos los casos el mejor desplazamiento es (0,0): sin desalineación sistemática. Valores 27–31 dB son habituales para texturas de 16 px de alta frecuencia reescaladas 1.5x; no hay un umbral "bueno" absoluto, la referencia es la comparación entre configuraciones.

## No verificado (limitaciones explícitas)
- Ghosting de **entidades, partículas y agua en movimiento**: no se pudo construir una escena dinámica reproducible con comandos (los objetos quedaron ocultos en el follaje); sin vectores propios cabe esperar estelas. NO se afirma que estén bien.
- Cielo/nubes/agua/vidrio **estáticos**: sin artefactos visibles y 40 dB en la escena estática (arriba).
- Flicker temporal: no medido objetivamente (sin métrica frame a frame sobre ráfagas).

## Sesión 6: ghosting de entidad (prueba reproducible, 2026-10-12)
Escena (`scripts/ghost-test.sh MODE OUT`, informe `scripts/ghost-report.py`): oveja blanca (NoAI) ante un muro de hormigón negro, cámara fija, `/tick freeze` + `/tick step 1`; ráfaga de 100 fotogramas por salto, la oveja se teletransporta 0,8 bloques por salto (≈50–60 px). 1280x720, Quality, sin FG. Se mide la luminancia en el área que la oveja acaba de dejar (nativo = 0 residuo) desde el primer fotograma en que aparece en su nueva posición.
Resultados (una sola tanda, un solo sujeto, sin repeticiones):
| Modo | Salto 1: fotogramas con residuo | Salto 2: fotogramas con residuo |
|---|---|---|
| Nativo | 0/81 | 6/77 (residuo propio del juego: valor máx. 16,4) |
| FSR 3.1.5 | 0/80 | 0/79 |
| FSR 4.1.1 | 0/81 | 3/81 (valor máx. 15,2, ≤ nativo) |
Conclusión limitada: **no se midió estela de ghosting** de FSR en este escenario (el área vacada vuelve al valor del nativo en el primer fotograma con la oveja en el nuevo sitio). Lo que NO demuestra: movimiento continuo, entidades con texturas finas, partículas, agua, hojas ni cristal; el salto es una desoclusión discreta. Las entidades siguen sin vectores de movimiento propios (el escalador usa solo reproyección de cámara), por lo que el ghosting en movimiento continuo sigue sin cuantificar.
Dato de alineación: el resultado depende de la sincronización del burst (la captura debe empezar antes del movimiento); la primera versión del experimento capturaba ya convergido y fue descartada.

## Sesión 7: ghosting con movimiento continuo (`scripts/motion-test.sh`, `scripts/motion-report.py`, 2026-10-12)
Escena determinista por índice de fotograma renderizado (mundo congelado con `/tick freeze`, nueva orden `motion` del arnés): muro de hormigón negro, cubo de hojas de roble, estanque de agua, panel de cristal azul y una oveja blanca. Fase A (90 fotogramas): cámara fija, la oveja cruza a 0,06 bloques/fotograma (~5,6 px/fotograma a 1280x720: movimiento rápido e independiente de la cámara). Fase B (90 fotogramas): oveja aparcada, la cámara gira 0,3°/fotograma sobre hojas/agua/cristal. 1280x720, Quality, sin FG; mismo índice = mismo estado de juego, así que se compara cada fotograma con el nativo.
Métricas: zona de estela = píxeles oscuros ahora en el nativo (<35) que fueron brillantes (>100) en alguno de los 8 fotogramas anteriores (≈1400 px de media, 80 fotogramas).
| Modo | Estela: luminancia media / máx. sobre el nativo | Píxeles fantasma (>+25 niveles) media / máx. | Fase A PSNR / SSIM vs nativo | Fase B (giro) PSNR / SSIM | Flicker temporal vs nativo (1,0 = igual) |
|---|---|---|---|---|---|
| FSR 3.1.5 Quality | **+12,5 / +20,1** | **15,7 % / 24,2 %** | 32,10 dB / 0,9651 | 29,35 dB / 0,9121 | 0,900 |
| FSR 4.1.1 Quality | **+1,3 / +3,5** | **1,06 % / 2,54 %** | 33,16 dB / 0,9736 | 29,84 dB / 0,9300 | 0,970 |
Conclusiones (una tanda, un sujeto; sin repeticiones ni IC): con una entidad cruzando rápido sin vectores de movimiento propios, **FSR 3.1.5 deja una estela claramente medible** y **FSR 4.1.1 la reduce ~10x** (1 % de píxeles de la zona frente a 16 %), sin llegar a cero. El flicker temporal de ambos es ≤ nativo (suavizan, no añaden parpadeo). La fase de giro (hojas/agua/cristal con reproyección de cámara correcta) da 29–30 dB / SSIM 0,91–0,93: la diferencia frente al nativo es sobre todo pérdida de detalle fino de la reconstrucción, no estelas. No cubre partículas ni agua en flujo (mundo congelado).
Vectores por entidad: NO implementados en la sesión 7 (implementados en la sesión 8, ver arriba). Requerirían una segunda salida de velocidad en el pipeline de entidades de Minecraft (mixin sobre los shaders de entidad y matrices previas por entidad, que el juego no conserva) — trabajo de varios días con riesgo alto de regresiones; dado que 4.1.1 ya reduce la estela 10x, no se acometió. Alternativa más barata no probada: máscara reactiva con las entidades.

## Sesión 10: calidad con la presentación por lotes y FG ×4 (contra fotogramas verdaderos renderizados)
`scripts/x3-ingame-report.py COARSE DENSE N` (ahora genérico en N; N=3 reproduce las cifras de la sesión 8). Giro de cámara (ROI sin HUD) / oveja que cruza:
| Modo | Giro PSNR / SSIM | Oveja PSNR / SSIM |
|---|---|---|
| ×3 por lotes (generado) | 34,29 dB / 0,963 | 30,89 dB / 0,976 |
| ×3 ruta anterior (generado) | 34,35 / 0,965 | 30,44 / 0,979 |
| ×4 por lotes (generado, 3 fotogramas) | 34,07 / 0,964 | 29,74 / 0,981 |
| Mezcla temporal (×3 / ×4) | 28,4 / 28,7 | 28,8 / 28,0 |
| Duplicar (×3 / ×4) | 27,0 / 27,2 | 27,4 / 26,7 |
La presentación por lotes no cambia la imagen generada (diferencias = ruido entre ejecuciones). Limitación de la medida: se captura la imagen generada antes del blit al swapchain, no el swapchain.
**Presupuesto de error (`tools/ai/error_budget.py`, escenas de la sesión 9, fase con cámara móvil):** entre el 72 % y el 99 % del error cuadrático de los fotogramas generados está en el 0,5–8,5 % de los píxeles (errores ≥16 niveles: movimiento/desoclusión/partículas). Un "denoiser" local perfecto subiría 0,05–1,5 dB; un corrector perfecto de esos errores, +6…+19 dB. Partículas: 21,8 dB, 8,5 % de píxeles con error grosero → la mejora de calidad real está en dar movimiento a las partículas (código pendiente de probar, `docs/STATUS.md`).

### Sesión 10: partículas con vectores propios
Ver `docs/PARTICLE_MV.md`: escena de partículas, generado − mezcla = +0,5 dB (cámara fija) / +1,3 dB (móvil) con vectores de partículas frente a −0,75 / +0,1 dB sin ellos (3 repeticiones); SSIM −0,016. ×4 por escenas: `docs/FG_X4.md`.


## Sesión 12 (corrector neuronal): ver `docs/NEURAL_EVALUATION.md` y `docs/evidence/neural/` (G | regla | red | verdad con mapa de error). No aprobado.

## Sesión 13: partículas con/sin vectores (misma grabación)
En `v3_part1` el humo sale duplicado con `FSR4_PARTICLE_MV=1` (generado B) y menos con los vectores desactivados (A); ver `ab_particle_regions.txt`. La mejora de +1,2 dB de la sesión 10 (contra la mezcla temporal) no se reproduce al compararse con el generador sin vectores sobre la verdad.

## Sesión 15: partículas (misma grabación, A/B, ticks correctos)
Ver `docs/PARTICLE_MV.md` (sesión 15): con la escala de ticks corregida, vectores de partícula vs sin vectores en la máscara de partículas: MAE 14,05 → 10,72 (part1), 17,56 → 15,40 (part2); la escala antigua (×6,8 en el arnés) da 19,68 y bordes duplicados. Corrige la conclusión de la sesión 13 ("el humo sale duplicado con MV=1"), que se debía a la escala, no al concepto.

## Sesión 15: desoclusión (prueba sintética determinista, generador real)
`tools/disocclusion/synth_disocc.py`: rectángulos opacos con textura que deslizan sobre un fondo texturado, planos exactos, verdad analítica; error en la zona descubierta ("reveal", visible en t pero tapada en F0), la zona tapada y el resto (MAE / % error grosero ≥16):
| escena | reveal generador | reveal mezcla | cover generador | rest generador |
|---|---|---|---|---|
| lenta, cámara fija | 0,80 / 1,7 % | 14,1 / 34,8 % | 4,75 / 9,9 % | 0,38 |
| rápida, cámara fija | 1,13 / 2,9 % | 14,0 / 34,8 % | 2,83 / 4,8 % | 0,44 |
| lenta, cámara móvil | 5,07 / 10,3 % | 18,0 / 53,4 % | 3,99 / 6,7 % | 1,66 |
| rápida, cámara móvil | 1,82 / 1,1 % | 17,7 / 52,1 % | 4,34 / 7,8 % | 1,95 |
| desplazamiento enorme (90 px) | 0,43 / 0,6 % | 12,4 / 28,1 % | 2,94 / 1,2 % | 0,71 |
La variante 0/2 del shader (la comprobación de consistencia de profundidad elige solo F1 donde F0 estaba tapado) ya reconstruye la desoclusión con planos exactos; la variante 1 ("fotograma más cercano") es mucho peor (reveal 8–10 MAE). Conclusión: **con profundidad y vectores correctos no hay trabajo pendiente de reconstrucción**; el error real de desoclusión viene de la inexactitud de los planos (cajas de entidades infladas, partículas, translúcidos), no del algoritmo de relleno. Residuo: bordes de avance (cover 3–5 MAE) por cuantización del splat a 1 px y cámara móvil lenta (reveal 5,07). No se probó inpainting neuronal: no hay información que reconstruir que no esté en F0/F1.

## Auditoría 2026-10-22
La mejora medida de partículas (humo 14,05→10,72 MAE en máscara; esporas 17,56→15,40 en la grabación indicada en `PARTICLE_MV.md`) sigue siendo evidencia histórica de replay, no una nueva prueba en juego. Las grabaciones `tick rate 1` conservadas en `session15-new` incluyen imágenes y planos A/B; `tools/particles/recording_audit.py` informa `replay_ready=True` y `ab_ready=True` para ambas. Se mantiene `FSR4_PARTICLE_MV` opt-in hasta medir calidad y coste GPU; el caso de humo semitransparente aún requiere máscara de cobertura alfa o equivalente validada. No se modificó el generador sin una nueva comparación A/B ejecutada.

Análisis offline adicional: en los 49 casos existentes de `FSR4-evidence-session9/disocclusion/cases` (98 salidas), se marcó una banda de 6 px alrededor de las discontinuidades de profundidad de `d0.f32` o `d1.f32` (diferencia máximo-mínimo en ventana 3×3 > 1e-4). Esa banda cubre el 17,41 % de los píxeles y acumula el 46,9 % del error absoluto RGB de `out_*.rgba` frente a `truth*.rgba` (MAE 1,384 en banda; MAE global 0,514). Es una localización de error en salidas históricas; los planos de esas escenas no prueban qué parte del error se debe al algoritmo de desoclusión frente a profundidad, vectores o contenido translúcido. No se ejecutó replay ni se abrió TEST3.

## Reanudación 2026-10-22: corrector localizado y FG ×4

`tools/disocclusion/edge_corrector.py` lee los casos de sesión 9 sin modificarlos. Define borde como rango de profundidad 3×3 > 1e-4 en F0 o F1 y dilata con un disco de radio 6 px. En los **49 casos/98 salidas**, esta definición cubre **17,70 %** de los píxeles y contiene **47,36 %** del MAE RGB; MAE global **0,514** y MAE en banda **1,375**. La implementación exacta de la máscara de la auditoría anterior no se conserva: la diferencia frente a 17,41 %/46,9 % impide llamarlo una reproducción numérica exacta, aunque ambos cálculos encuentran la misma concentración. En cámara fija la banda contiene 19,2 % del error, y con cámara móvil 51,3 %. El hallazgo describe dónde está el error, pero no identifica por sí solo el color correcto ni su causa.

Se implementaron tres reglas offline, **solo dentro de máscaras de profundidad**, sin escribir sobre las grabaciones: (1) fotograma temporalmente más cercano, (2) F1, y (3) F1 únicamente donde F0/F1 concuerdan dentro de 4 niveles RGB y el generado se desvía ≥8. Se probaron la banda completa y su intersección con un desacuerdo relativo de profundidad >15 %. Para comparar se tomaron 17 casos espaciados cada tres de los 49 (34 salidas), alternando casos de desarrollo y reserva. En cámara móvil de reserva (10 salidas):

| Salida | PSNR | SSIM | MAE RGB | MAE banda | Ghost proxy | Nitidez proxy |
|---|---:|---:|---:|---:|---:|---:|
| Generador actual | 34,70 dB | 0,9754 | 0,736 | 1,749 | 0,0835 % | 0,962 |
| Cercano en banda | 26,40 dB | 0,9035 | 2,573 | 9,949 | 0,4977 % | 0,979 |
| Cercano con desacuerdo de profundidad | 27,11 dB | 0,9453 | 1,819 | 6,884 | 0,0790 % | 0,954 |
| Regla de acuerdo en banda | 34,29 dB | 0,9685 | 0,827 | 2,108 | 0,1949 % | 0,977 |
| Regla de acuerdo con desacuerdo de profundidad | 34,70 dB | 0,9753 | 0,737 | 1,757 | 0,0847 % | 0,963 |

Ghost proxy = salida próxima a la mezcla temporal (<6 niveles), verdad alejada de ella (≥24) y error ≥16; nitidez proxy = energía de gradiente de luminancia de la salida dividida por la de la verdad. Ninguna regla supera al generador en reserva móvil. En cámara fija, el reemplazo cercano gana 0,6–0,9 dB, pero el generador ya está en 55–58 dB y la regresión móvil es grave. No se integra el corrector. La coincidencia de profundidad en la misma coordenada de pantalla no constituye reproyección válida con movimiento; el shader actual ya usa MV y prueba consistencia de profundidad en coordenadas reproyectadas. TEST3 siguió sellado.

Replay GPU nuevo de FG ×4 con verdad analítica de `tools/disocclusion/synth_disocc.py --keep`: 3 generados por par en cinco escenas opacas. PSNR/SSIM del generador: lenta fija 44,15/0,9975; rápida fija 41,35/0,9948; lenta con cámara 38,77/0,9849; rápida con cámara 35,51/0,9742; desplazamiento 90 px 37,45/0,9874. La mezcla temporal obtuvo 22,30–31,50 dB. El ghost proxy del generador fue 0,004–0,038 %, frente a 2,35–12,21 % de la mezcla; la nitidez relativa fue 0,877–0,991. Estas son escenas sintéticas con profundidad y MV exactos, no una prueba de calidad de todos los contenidos de Minecraft. Datos por fotograma: `docs/evidence/session16/`.

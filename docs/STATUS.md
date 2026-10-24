# Estado (2026-10-22, rama `experimental/ai-performance`; estable: tag `stable-final-phase` = a334c7a en `master`)

## Reanudación 2026-10-22 desde `8b26e6e`
- Se inició en `experimental/ai-performance` con árbol limpio. No se repitió la recuperación de datos ni se abrió TEST3. No se borraron mundos ni datos y no se modificaron modelos de Ollama, BIOS, kernel, drivers, voltajes ni configuración de monitores; las pruebas en juego se hicieron en el perfil aislado `fabric/run`, en HDMI-A-2, y se restauraron su configuración y opciones tras los benchmarks.
- `/dev/dri/renderD128` accesible; `vulkaninfo --summary` detectó **AMD Radeon RX 9060 XT (RADV GFX1200)**, Mesa 26.2.4. CMake/Ninja compiló la biblioteca nativa. Gradle offline `compileJava compileClientJava test` pasó **23 tests Java**. `prep_test`, `fgx3_test`, `ipc_test 4.1.1` y `gpu_ipc_test 4.1.1` pasaron. El runtime AMD confirmó el proveedor de reescalado **4.1.1**; la ruta GPU/DMA-BUF y FG 4.0.1 del helper pasaron. No se creó paquete ni instalador.
- En juego, a 1920×1080/Quality/VSync OFF, medias de dos ventanas de 200 ticks por modo: nativo **1210** presentados/s; FSR 4.1.1 **547**; FG ×4 propio **1869** (467 renderizados + 1402 generados); FSR 4.1.1 + FG ×4 **1437** (359 + 1078). Se cuentan llamadas `vkQueuePresentKHR`, no fotogramas físicamente visibles en el monitor de 60 Hz. GPU del dispatch FSR: 0,827 ms solo y 0,929 ms con FG ×4. Ver `docs/BENCHMARKS.md` y `docs/evidence/session16/`.
- Desoclusión: los 49 casos/98 salidas históricos dieron, con la máscara circular nueva de radio 6 px, **17,70 % de píxeles y 47,36 % del MAE RGB**, cerca del 17,41 %/46,9 % anterior. El corrector offline de profundidad y máscaras se implementó y comparó en desarrollo/reserva; en cámara móvil de reserva, la sustitución temporal cayó 34,70→26,40 dB y la regla conservadora 34,70→34,29 dB, con peor SSIM/MAE/ghosting. No se integró ni se entrenó una red; TEST3 sigue sellado. Ver `docs/VISUAL_QUALITY.md`.
- FG ×4 en cinco escenas sintéticas con verdad analítica: PSNR **35,5–44,1 dB**, SSIM **0,974–0,998**, ghost proxy **0,004–0,038 %**, nitidez relativa **0,877–0,991**. El pase de tres generados a 720p costó **0,5774 ms GPU**; int64 **0,5352 ms**, con RGBA idénticos. En juego, int64 dio 1897 presentados/s entre dos controles sin int64 de 1869 y 1880; la mejora pequeña no es concluyente, así que continúa opt-in.
- La validación Vulkan de FSR 4.1.1 + FG ×4 detectó en el primer intento 25 objetos vivos al cerrar `VkDevice`; se añadió limpieza del pase FG y cierre anticipado del runtime. La recompilación nativa/Java y `fgx3_test` pasaron. La repetición de validación determinará si la corrección queda verificada.


## Auditoría de continuidad (2026-10-22, Codex)
- Rama `experimental/ai-performance`, HEAD `324a98b` al iniciar; había cambios sin commit en seis documentos y un script nuevo, conservados y revisados en esta sesión. La corrección por desplazamiento medido de partículas está en Git (`31cdf1d`), igual que la migración documentada (`324a98b`). No se han modificado `master` ni `stable-final-phase` (`a334c7a`).
- Almacenamiento ya preparado: `sda3` (repositorio, ~58 GiB libres en la auditoría previa) y `sdc1` (Steam, ~129 GiB libres en la auditoría previa) son discos distintos. `NeuralCraft-Data` existe fuera de `steamapps`; copias de dataset/evidencias y originales presentes. El registro de 2026-10-16 informa `rsync -rcn` completo sin diferencias salvo dos enlaces absolutos de session8 convertidos a relativos. La auditoría anterior con `rsync -ain --delete` no detectó diferencias de ficheros en dataset y sesiones 9/11/12/13/15; session8 solo informó esos enlaces y el mtime de su directorio. El SHA-256 de una captura de session15 coincidió entre original y copia. Las rutas Python/shell resuelven la copia verificada.
- Las capturas `tick rate 1` de humo/esporas en `NeuralCraft-Data/evidence/session15-new` conservan 150 PNG y 150 planos de cada tipo `d/m/da/ma`, además de 150 marcas `cam`, por escena. `tools/particles/recording_audit.py` devuelve `replay_ready=True` y `ab_ready=True` en ambas. La afirmación anterior de que faltaban los planos era incorrecta. En siete cuadros fijos por escena (índices 10, 30, 50, 70, 90, 110, 130), los planos A/B difieren en el 5,01 % de píxeles del humo y el 3,25 % de esporas; la diferencia media de vector dentro de esas máscaras es 3,34 y 5,28 px, respectivamente. Son estadísticas de entrada, no una nueva medición de calidad de FG.
- Permisos: `findmnt` muestra btrfs `rw`; `.git` y `~/.gradle` pertenecen al usuario, aceptaron crear y cerrar archivos temporales con nombre, y `.git/index.lock` no existía. La restricción `.git` de la sesión anterior no se reproduce aquí. La compilación nativa completa terminó bien con CMake/Ninja en `/tmp/fsr4-cmake-audit` (la caché `build/cmake` existente aún apunta a una ruta antigua). `fgx3_test` terminó con código 2, `no discrete GPU`. Gradle `--offline compileJava compileClientJava test` no llegó a compilar: `FileLockContentionHandler` no puede consultar interfaces de red (`SocketException: Operation not permitted`), una restricción del sandbox ajena a permisos de la caché. Java queda sin validar en esta sesión.
- GPU: `/dev/dri` ausente; `vulkaninfo --summary` no puede enumerar dispositivos DRM ni conectar con X. Sin pruebas Vulkan, juego ni benchmarks nuevos. El entorno `~/neuralvenv/v` documentado en sesión 14 ya no existe aquí; `python3` del host tiene PyTorch `2.14.0+cu130` (sin ROCm). No se entrenó ninguna red ni se utilizó TEST3 para seleccionar modelos.
- Desoclusión: lectura offline de las 49 escenas históricas de sesión 9 (98 salidas) concentra el 46,9 % del error RGB en el 17,41 % de píxeles situados a ≤6 px de un borde de profundidad en F0 o F1. Ver método y límites en `docs/VISUAL_QUALITY.md`; no es una nueva prueba GPU.
- Versiones detectadas sin cambiar el sistema: OpenJDK 26.0.2, Minecraft 26.3/Fabric Loader 0.19.5 (configuración), Mesa Vulkan 26.2.4, Vulkan loader 1.4.341, kernel 7.2.9; GE-Proton11-6 y árbol local FidelityFX SDK presentes. No se pudo validar el proveedor AMD en vivo.

## Sesión 15 (2026-10-16)
- Disco: 3,5 GB libres al inicio (100 %), nada borrado salvo planos propios regenerables (≈6 GB de grabaciones de esta sesión, tras analizarlas). El 99 % restante son datos personales/evidencias: no tocado.
- **Humo duplicado: causa demostrada** (tiempo de pared/50 con tope 4 en vez de ticks de juego; arnés a 0,6 s/fotograma) y **corregido** (`ParticleMotion.frameTicks()`); con la escala correcta los vectores de partícula mejoran la máscara de partículas −24 % MAE (part1) / −12 % (part2) y el barrido de escala/signo confirma el óptimo. Ver `docs/PARTICLE_MV.md` (sesión 15). Sigue opt-in.
- Herramientas nuevas: `tools/particles/{synth_particles,real_ablation,mv_vs_flow}.py`, `TICKRATE=` en `scripts/x3-scene.sh`.
- NO hecho en esta sesión (sin evidencia, por tanto sin afirmaciones): desoclusión (A–G), IA especializada, coste neuronal, regresión en juego F6–F10/FG ×2/×3/×4, benchmarks.

## Sesión 10 (2026-10-13): presentación por lotes, FG ×4, validación, IA, trabajo pendiente de probar
**Verificado (medido / validado):**
- **Presentación por lotes** de FG ×3/×4 propio (`BatchPresent`): un `submit()` por fotograma renderizado en vez de 3; ×3 sube de ≈905 a 1599–1625 presentados/s, GPU 67 % → 99 %. Detalle y datos: `docs/PRESENTATION_ARCHITECTURE.md`. Un hilo de presentación separado se evaluó y se descartó con datos (present = 0,04 ms de CPU; el bloqueo está en `submit()`).
- **FG ×4 propio** (`fgMultiplier: 4` en `config/fsr4linux.json`; NO expuesto en el menú F6): 1784–1862 presentados/s (≈450 renderizados); calidad vs verdad en juego ≈ ×3. Ver `docs/FG_X4.md`.
- **Validación Vulkan** (capa Khronos + sincronización, `scripts/validate-vk.sh`): limpia en ×3, ×4 y FSR 4.1.1 + ×3 con cambios de pantalla completa; corregidas dos copias fuera de rango en cambios de tamaño y una carrera `BridgeClient::stop()`/`start()` al cerrar.
- **IA**: red residual pequeña (1,5 k y 23 k pesos) sin mejora medible; análisis de presupuesto de error (`tools/ai/`, `docs/AI_RESEARCH.md`).
- Regresión en juego (`scripts/regression-x3.sh`) pasada tras la presentación por lotes (`docs/evidence/session10/regression_x3_batch.txt`); tests Java y `fgx3_test` pasados.
- FSR 4.1.1 oficial, AMD FG ×2 y el helper de Wine no se tocaron funcionalmente.

**Compilado y probado en la segunda parte de la sesión 10 (monitor vertical de 60 Hz, sin tocar la partida del monitor principal):**
- **Vectores de movimiento de partículas** (`docs/PARTICLE_MV.md`): FUNCIONAN y mejoran la escena de partículas de forma reproducible (generado − mezcla: +0,5 dB cámara fija, +1,2…+1,3 dB móvil, frente a −0,7 y ±0,2 sin ellos; 3 repeticiones), SSIM −0,016, coste ≈−3,8 % de presentados en una escena sin partículas. Siguen **opt-in** (`FSR4_PARTICLE_MV=1`). Validación Vulkan limpia.
- **Shader FG de una pasada con atómicos de 64 bits**: compila, salida **idéntica byte a byte** a la de 2 pasadas en reproducción offline (3 casos), −5 % de tiempo GPU a 720p en reproducción, pero **sin diferencia medible en juego** (×4: 1865 vs 1867 y 1886 vs 1887 presentados/s). Queda **opt-in** (`FSR4_INT64=1`, mixin de dispositivo; `FSR4_X3_NO_INT64=1` lo desactiva aunque esté la característica). Validación limpia.
- FG ×4 offline por escenas (`docs/FG_X4.md`).
- Sonda de presentación y `scripts/probe-run.sh`: usadas (diagnóstico).
- Tests: `prep_test` PASS, `fgx3_test` PASS (nota: el control negativo `FGX3_TEST_BADMV=1` **no falla** —ya pasaba en a27c1f6—; no discrimina y no es una prueba válida del control), 23 tests Java PASS; validación Khronos (×3, FSR+×3, con partículas, pantalla completa) limpia.
- Ventanas de prueba en el monitor vertical: script temporal de KWin cargado por DBus (`/tmp/claude-1000/kw/mc_to_vertical.js`, mueve las ventanas "Minecraft"/helper `.exe` a HDMI-A-2); no modifica la configuración de KDE y se descarga con `busctl --user call org.kde.KWin /Scripting org.kde.kwin.Scripting unloadScript s mc_to_vertical`.

**Reglas para las próximas pruebas gráficas:** NO abrir ventanas en el monitor principal (HDMI-A-1, 100 Hz) mientras se juega; usar el monitor vertical (HDMI-A-2, 60 Hz, 1080x1920) detectándolo con `kscreen-doctor -o`, sin cambiar la configuración de KDE; usar la GPU compartida contamina las medidas y molesta la partida: pedir autorización antes. Las medidas de VSync a 100 Hz requieren el monitor principal: esas, solo con permiso explícito.

**Pruebas pendientes (todas requieren permiso para usar la GPU; usar el monitor vertical):**
1. Medir el coste GPU del modo de partículas en la escena con partículas y entender el −3,8 % en escena sin partículas.
2. Partículas con FSR 4.1.1 + FG ×3 en juego (truth report) y con AMD FG ×2 (¿ayudan las nuevas profundidades/vectores?).
3. Repetir `scripts/bench-suite2.sh` tras cualquier cambio de shader; objetivo abierto: 2000 presentados/s con ×4 (hoy 1865–1887; las optimizaciones del shader no han movido el resultado).
4. Medidas de VSync a 100 Hz (monitor principal): solo con permiso explícito.
5. Imágenes realmente visibles con VSync OFF: requiere método externo (captura), sin resolver.



## Investigación: FSR 4.1.1 nativo en Vulkan sin Wine (2026-10-12) — BLOQUEADO
Veredicto y evidencia en `docs/NATIVE_FSR4_FEASIBILITY.md`: el proveedor FSR 4 solo existe dentro de DLL firmadas de DX12 (sin fuentes), el SDK no tiene backend Vulkan, el modelo no se distribuye aparte, depende de extensiones del driver D3D12 de AMD, y la licencia prohíbe la ingeniería inversa. No hay prototipo ni rama. La ruta Wine/GE-Proton queda intacta y es la recomendada.


## Sesión 9 (2026-10-12): estabilización y calidad de FG ×3
- **CRC (causa demostrada y corregida).** Fallo del autotest de planos compartidos: reproducido con `scripts/crc-stress.sh` (helper nuevo por iteración con todos los núcleos ocupados): **4 de 60** inicializaciones fallaron (sin carga: 0 fallos en 180 inicializaciones; la carga lo desencadena). Diagnóstico (`FSR4_CRC_DIAG=1`): el CRC que ve el helper en el plano 0 es el del plano 2 nativo (permutación cíclica de los planos 0–2) y en 5 de 30 asignaciones el orden por número de fd de los dma-buf del helper ≠ orden de creación (Wine reutiliza números de fd). **No era** sincronización, visibilidad de memoria ni vida útil: una relectura de los mismos datos daba lo mismo (permanente) y los datos eran correctos, solo estaban asignados al plano equivocado. Corrección: ordenar los dma-buf nuevos por **número de inodo** (se asigna en la creación y crece). Verificación: 80 reinicios con carga + 300 reasignaciones: 0 fallos; **prueba determinista** `FSR4_CRC_SHUFFLE=1` (simula el orden permutado): con la ordenación por inodo 10/10 OK, sin ella (`FSR4_CRC_NOSORT=1`) FALLA con la misma firma de CRC (`0x94347fc3` vs `0xf2c89d8d`). Limitación: en la tanda posterior a la corrección la permutación natural no volvió a ocurrir (0/80), así que la evidencia fuerte es la identidad de firma + la prueba determinista. El autotest sigue activo.
- **Calidad visual**: escenas de partículas, agua animada, vegetación, transparencias, desoclusión y animales con IA, y UI translúcida (ver `docs/VISUAL_QUALITY.md`, sesión 9). Cámara móvil: +7…+10 dB sobre mezclar; cámara fija: ≈ igual a la mezcla (−0,1…−1,5 dB); **partículas: sin mejora posible con los datos actuales** (sin vectores de partícula; la estimación por bloques no mejoró). Mejoras con medida: fallback temporal más cercano salvo oclusión (+0,5…+1,5 dB cámara fija), compuesto multiplicativo de UI translúcida (44→53 dB).
- **FSR 4.1.1 + FG ×3 contra referencias reales**: giro 35,85 dB / SSIM 0,967 frente a FSR verdadero (duplicar 28,5, mezcla 30,1), error grosero 0,04 %.
- **Rendimiento**: 826 FPS presentados (275 renderizados + 551 generados) a 1080p con FSR 4.1.1 Quality; solo ×3 864; GPU del par 0,52–0,84 ms (0,707 ms medido en juego con cámara fija); optimización de la búsqueda de huecos (0,78→0,44 ms en reproducción con huecos; sin cambio medible en el juego con cámara fija). Latencia estimada 4,3 ms (modelo).
- **Regresiones**: `scripts/regression-x3.sh` (F7/F8/F9/F10, calidad, pantalla completa, menú con clic real en el selector, Nether y Overworld, salir y volver a entrar al mundo, helper muerto y recuperación con F10/F7, persistencia): PASS (`docs/evidence/session9/regression_x3.txt`). 23 Java, prep_test, fgx3_test (5 casos), ipc/gpu_ipc 4.1.1 y 3.1.5, crc_stress: PASS.
- Versión experimental estable: FSR 4.1.1 + FG ×3 propio (`fgMultiplier=3`, desactivado por defecto, EXPERIMENTAL).


## Sesión 8 (2026-10-12): vectores de movimiento + FG ×3 experimental
- **Recuperación tras apagón**: repo íntegro (`git fsck` limpio, árbol limpio en 8666386), sin trabajo perdido.
- **Error preexistente corregido**: el pase de preparación leía la profundidad mientras Minecraft la limpiaba (carrera, planos de profundidad/MV parcialmente a cero) → barrera final. **Y de los vectores espejada** (render targets de abajo arriba) → corregida (giro +1,1 dB / SSIM +0,056 en FSR 3.1.5; flicker de 4.1.1 0,85 → 0,98).
- **Vectores por entidad** (rígidos, por caja, sin huesos, máx. 48): **activos por defecto**; estela de FSR 3.1.5 13 % → 5 %, 4.1.1 2,65 % → 2,3 %, FG ×2 AMD en la zona móvil +3,8 dB (contra fotograma verdadero). `FSR4_ENTITY_MV=0` los desactiva.
- **FG ×2 AMD**: la escala Y del vector es +1 (una medición contra un proxy engañoso dijo −1: retractada); contra el intermedio verdadero 33,7 dB (giro) / 32,0 dB (oveja).
- **FG ×3 propio (EXPERIMENTAL, desactivado por defecto, selector en F6)**: prototipo Vulkan aislado + integración; giro 34,1 dB, oveja 31,3 dB contra verdad (duplicar 26,8 / mezcla 28,3); 0,68 ms GPU y 58 MiB a 1080p; hasta 852 FPS presentados (3 imágenes por fotograma renderizado) — **no supera al nativo (1163) en la escena limitada por CPU y no se alcanzan 2000 FPS**. Ver `docs/FG_X3.md`.
- Pruebas: 23 Java, prep_test (7 casos), **fgx3_test** (4 casos + control negativo), ipc_test y gpu_ipc_test 4.1.1/3.1.5, estrés en juego (F7/F8/F9/F10, pantalla completa, menú, clic en el selector, muerte del helper → desactivación con aviso): PASS.
- Reanudar: `scripts/build-native.sh && scripts/build-shaders.sh && scripts/install-runtime.sh`; arnés de calidad: `scripts/motion-test.sh`, `scripts/motion-report.py`, `scripts/x2-ingame-report.py`, `scripts/x3-ingame-report.py`.


## Recuperación tras apagón (2026-10-12)
- Estado: árbol limpio en `8666386` (rama `experimental/ai-performance`), sin merge/rebase/lock/stash interrumpido, `git fsck` sin errores. No se perdió nada; no había cambios sin commitear ni archivos a medio escribir (sesión 7 completa en el último commit). Copia de seguridad del `.git` en `~/Escritorio/Minecraft-FSR4-Linux-backup-*.git-only`.
- Revalidado: compilación nativa + helper, Gradle build, 22 tests Java (`--rerun-tasks`), prep_test, ipc_test 4.1.1/3.1.5, gpu_ipc_test 4.1.1/3.1.5, y sesión real en el juego (FSR 4.1.1 auténtico + FG 4.0.1; F7/F8/F9/F10 en vivo): PASS.
- Sin trabajo a medias que terminar. Pendiente real (sin cambios): MV por entidad, FG ×3 integrado, latencia con hardware.


## Sesión 7: perfilado, FG, FP16, ghosting continuo
- **Perfilado real** con marcas de tiempo GPU (`FSR4_GPU_TS=1`): FSR 4.1.1 1080p = prep 0.058 + cadena 1.05 (dispatch AMD 0.93) + copia 0.016 + resto 0.77 ms; puente ≈0.19 ms (10 %). Ver `docs/PERFORMANCE_OPTIMIZATIONS.md`.
- **Optimización**: ninguna candidata mejora de forma real y estable (cola compute 0 %, solapamiento sin esperar la salida ≤+3–5 %). Resultado negativo documentado; siguiente cuello de botella = dispatch de AMD FSR 4.1.1 + trabajo GPU fijo de Minecraft (≈0.8 ms).
- **FG**: desglose medido (dispatch 1.45 ms, IPC 0.01 ms, `submit()` bloqueado 1.15 ms de CPU, huecos GPU 2.5–3.5 ms/fotograma); modelo AMD intacto.
- **FP16 coopmat**: prototipo funcional, verificado y medido (`tools/coopmat`, `docs/AI_RESEARCH.md`); FP8/BF16 no enumerados en Vulkan/RADV.
- **FG ×3**: solo estudio offline por reproyección de rotación (33.5 dB vs 23.4 dB duplicando); no integrado (`docs/EXPERIMENTAL_BACKENDS.md`).
- **Ghosting continuo**: FSR 3.1.5 deja estela (15.7 % píxeles de la zona), FSR 4.1.1 ~10x menos (1.06 %). Sin vectores por entidad (no implementados). `docs/VISUAL_QUALITY.md`.
- **Menú con clics sintéticos**, dimensiones, respawn, pantalla completa, recuperación del helper (kill/freeze) verificados; el "fallo intermitente de FG OFF" era un error de análisis (retirado). Estado `PENDING` añadido a FG.
- Tests: 22 Java, prep_test, ipc_test 4.1.1/3.1.5, gpu_ipc_test 4.1.1/3.1.5: PASS (tras recompilar los tests en `build/cmake/`; los binarios de `build/` eran obsoletos).
- Reanudar: `scripts/build-native.sh` deja los tests actuales en `build/cmake/` (ejecutar desde la raíz: `build/cmake/ipc_test 4.1.1 OUT seq reset resize negative crash latency`).

# Estado al cierre de sesión (2026-10-11, sesión 5: finalización de FSR 4.1.1 y Frame Generation)

## Resumen de la fase de finalización (ver VISUAL_QUALITY.md, FRAME_GENERATION.md, TEST_RESULTS.md)
- **Controles verificados en el juego**: F7 Frame Generation, F8 versión (3.1.5 ⇄ 4.1.1, solo disponibles), F9 calidad, F10 reescalado; F6 abre el **menú de ajustes** (también botón "FSR..." en Opciones > Vídeo). Teclas reasignables en Controles de Minecraft (categoría del mod). Estado persistido en `config/fsr4linux.json` y **restaurado al arrancar** (probado: FSR 4.1.1 Balanced + FG activos tras reinicio; cambios en vivo guardados).
- **Calidad**: corregidos el signo del jitter Y (+2.7 dB) y la profundidad (solo 3.5 % de píxeles válidos: Minecraft la limpia antes de la mano; ahora 97 %); anillo de uniformes; pasada de mano. HUD de FG separado (HUDLess). Ver VISUAL_QUALITY.md.
- **Recuperación**: helper muerto o congelado → FSR/FG se desactivan con aviso y se puede reintentar; vigilante de bloqueo de GPU. Defecto de semáforo obsoleto corregido.
- **Pruebas**: 22 unitarios Java, prep_test, ipc_test (4.1.1 y 3.1.5), gpu_ipc_test (seq/latency/resize/fg), estrés de integración (F7/F8/F9/F10 + pantalla completa + menú): todos PASS.
- **Sin regresión de rendimiento**: FSR 4.1.1 Quality 1080p ≈545 FPS; FG ≈ igual que en la sesión 4.
- **Pendiente / no verificado**: ghosting de entidades, partículas y agua en movimiento (sin MV por objeto); flicker temporal sin métrica; latencia extremo a extremo con hardware; probar las interacciones del menú con clics reales (se verificó el diseño y los datos en vivo; los botones delegan en las mismas rutas que las teclas). El mundo de pruebas anterior se conservó renombrado (`fabric/run/saves/FSRTest.polluted-by-quality-tests-2026-10-11`); el harness crea uno limpio con la misma semilla.

# (sesión 4: Frame Generation)

## Frame Generation 4.0.1 (F7) — ver docs/FRAME_GENERATION.md
- **Funciona dentro de Minecraft**: proveedor 4.0.1 confirmado por el runtime AMD, fotogramas generados auténticos (E5-2 sintético vs verdad: 40–43 dB; en el juego: estático 50–56 dB vs real, giro 60°/s 27–29 dB, HUD estable), presentados antes del real. Modos con y sin upscaler. F7/F8/F9/F10 verificadas en vivo (estrés).
- **Dependencia**: 4.0.1 necesita `amdxcffx64.dll` de AMD en el prefijo (sin él solo 3.1.6; mensaje de error explícito). No se redistribuye.
- **Objetivo 2000 FPS: NO alcanzable** (medido: máx. 502–632 FPS presentados desde 1123 nativos; FG cuesta 1.3–2.0 ms GPU a 1080p > 0.89 ms del fotograma nativo). VSync 100 Hz: 50 renderizados + 50 generados = 100 presentados (sin ganancia, +8 ms de latencia).
- Pendiente FG: medir latencia extremo a extremo con hardware; HUD separado (hudless) para evitar interpolarlo; async compute del FG; probar el proveedor 3.1.6 en el juego; pacing no bloqueante (hilo de presentación).


## HITO: FSR 4.1.1 oficial de AMD funciona DENTRO de Minecraft 26.3 (verificado)
- F8 selecciona 4.1.1; el runtime AMD confirma `provider 4.1.1`, hay dispatches reales por fotograma y el resultado se presenta (capturas y logs en `docs/evidence/ingame/`). Sin fallback silencioso (el estado "ejecutando" sólo sale del runtime AMD).
- F9 cambia calidad, F10 activa/desactiva, todo en caliente, con persistencia.
- Mundo a resolución interna + HUD nativo verificado visualmente.
- Bug raíz de 4.1.1 en juego: faltaba AUTO_EXPOSURE (`ffxDispatch rc=6`). Corregido.
- Timeout exit 124 anterior: no reproducible; ver TEST_RESULTS.md.

## Rendimiento — optimización del camino crítico (sesión 3) — ver BENCHMARKS.md
- Perfilado por pasos (submit / espera GPU / IPC / CPU helper / espera fence) en `TestHarness` (CSV: submit_ms, gpuwait_ms, ipc_ms, helper_rec_ms, helper_wait_ms).
- Causas del coste: (1) helper dormía `Sleep(1)` y el cliente `sleep 50us` (≈0.4–0.5 ms/frame de latencia de despertar); (2) dos esperas CPU por frame (vkWaitSemaphores + fence D3D12) que serializaban CPU↔GPU y dejaban burbujas en la GPU; (3) 4 copias GPU en el helper.
- Arreglos (cada uno medido antes/después, 3 reps): busy-poll acotado (+22 %), **fence compartida D3D12↔timeline Vulkan (E4, +56 %)**, dispatch directo sobre texturas compartidas (+5 %). FSR 4.1.1 Quality 1080p: **270 → 535 FPS**.
- Restaurar versión estable: `git checkout stable-bridge-v1` (original), `stable-bridge-v2-fence` (fence, con copias); o por entorno: `FSR4_SYNC=cpu` (sin fence), `FSR4_LEGACY_WAIT=1` (sondeo antiguo), `FSR4_NO_DIRECT=1` (con copias en el helper).
- Resultado honesto: **FSR 4.1.1 no supera al nativo en ningún escenario medido** (dispatch 0.93 ms @1080p, 4.1 ms @4K > fotograma nativo vanilla); FSR 3.1.5 Ultra Performance gana +8 % solo en 4K con preset Fabulous + RD 32.

## Pendiente
0. (hecho en sesión 3) optimización del puente; pendiente: validar la fence con resize de ventana/pantalla completa real en juego y con latencia extremo a extremo medida con hardware.
1. Pantalla de ajustes en el juego (backend, calidad, nitidez, resoluciones, backend activo, FPS/GPU ms, errores, teclas).
2. Investigar si FSR 4.1.1 puede abaratarse (dispatch ∝ píxeles de salida; no controlable desde el mod) o ofrecer salida a menor resolución + escalado final; evaluar renderizadores más pesados donde FSR gane.
3. Calidad: validar MVs con entidades/partículas/agua; comparación objetiva; pruebas de fullscreen/redimensionado.
4. Native AA en juego; FSR 3.1.4 Vulkan (no implementado).

## Cómo reanudar
- `scripts/build-native.sh && scripts/install-runtime.sh`; `scripts/build-shaders.sh` si cambia shaders/.
- Mod: `toolbox run -c fedora-toolbox-42 bash -c 'cd fabric && JAVA_HOME=/usr/lib/jvm/java-25-openjdk ./gradlew --no-daemon build'` (regenerar `~/.cache/fsr4-linux/cp.txt` con `-q printRunClasspath` si cambia).
- Prueba 4.1.1: escribir `fabric/run/config/fsr4linux.json` con enabled=true, backend FSR4_1_1 y ejecutar
  `FSR4_SCRIPT="wait 40; cmd /tp @s 300 140 300; wait 80; fly; look 30 20; wait 160; state; shot fsr411; quit" scripts/run-client.sh --width 1280 --height 720`
  (con enabled=true el juego activa FSR solo al arrancar; `key toggle` lo APAGARÍA). Revisar `fabric/run/fsr4-test.log`.
- Bench reproducible: `WAIT_IDLE=1 scripts/bench-run.sh OUT.csv W H "BACKEND QUALITY ENABLED" "pasos del harness" [renderDistance]` (+`OPT_OVERRIDES='graphicsPreset="fabulous" ...'`) y `scripts/bench-report.py OUT.csv`; comparación visual `scripts/psnr.py`.
- Para benchmarks: `inactivityFpsLimit:"minimized"`, `enableVsync:false` en `fabric/run/options.txt` y máquina en reposo.
- Git: sólo commits locales, sin push.

## Sesión 11 — corrector neuronal
Hecho y medido: modelo entrenado (selres8, 3 417 pesos), motor Vulkan con paridad máx. 1 nivel vs PyTorch, coste 4,8 ms/1080p. NO hecho: integración en Minecraft, menú F6, benchmarks en juego (bloqueados porque el modelo no pasa la evaluación en TEST: regresión en 3 de 4 escenas). FSR 4.1.1 / FG sin cambios. Commits locales, sin push.

## Sesión 12 — corrector neuronal, segunda fase
Hecho y medido: dataset 31 escenas, ROCm verificado, 8 familias comparadas, TEST2 sellado evaluado una vez, coste 1080p medido. Decisión de integración: NO (criterios no cumplidos: regresión en transp_d, ghosting en partículas). Sin cambios en FSR 4.1.1 / FG; menú F6 sin sección neuronal. Commits locales, sin push.

## Sesión 13 (pausada por el usuario)
Hecho: auditoría de espacio (disco 99 % → 92 % tras vaciar el usuario la papelera de 33 GB; nada borrado por mí salvo planos propios ya convertidos), métricas de ghosting + controles negativos, A/B de vectores de partículas (volcado `FSR4_PARTICLE_AB=1` en el mod, `scripts/neural-data-ab.sh`), TEST3 grabado y sellado (6 escenas `t3_*`), criterios v2, `train3.py`. Resultados: ver `NEURAL_EVALUATION.md` (sesión 13). Pendiente: relanzar k3–k5, diagnosticar por qué los vectores de partículas empeoran, fase de rendimiento (salto de teselas / reglas), regresiones F6–F10/FSR/FG, TEST3 una vez, decisión. Sin cambios en FSR 4.1.1, FG ni menú; IA no integrada.

## Sesión 14 (inicio, 2026-10-15)
- Disco `/var/home` al 100 % (3,9 GB libres); nada borrado. Entorno válido: `~/neuralvenv/v` (torch 2.9.1+rocm6.4).
- k3/k4/k5 (s13) murieron en it 500 sin checkpoint: no reanudables. k5 era idéntico a k4.
- Relanzados en serie `s14_k3` (mixpen 0.5) y `s14_k4` (mixpen 0.0) con `tools/ai/neural/train3.py`; logs en `tools/ai/neural/runs/s14_*.log`. Sin evaluar todavía; TEST3 sigue sellado.
- Pendiente: partículas, métricas, estrategia neuronal, rendimiento 1080p, documentación.
- Resultado s14 (validación v2, 21 escenas, TEST3 intacto): ambos terminaron por paciencia, ninguno supera al generador G.
  - k3 (mixpen 0.5): mejor en it 500, dPSNR −0,42 dB; empeora después (−0,66 dB, MAE mov +0,5). Descartado.
  - k4 (mixpen 0.0): mejor en it 1500, dPSNR −0,25 dB (min −2,37), dMAEmov −0,05, dmix% −0,30, dHF +0,01. Sigue por debajo de G en PSNR → no aprobado.
  - Conclusión: la selección dura con CNN pequeña no mejora a G; descartar la línea train3 y pasar a corrección de desoclusión/máscaras.

### Sesión 15 (continuación)
- Partículas: corrección definitiva = desplazamiento medido por partícula (no velocidad×tiempo): `/tick rate 1` MAE en máscara 16,30 → 13,93; 12 ticks/fotograma 20,96 → 18,70; `/tick freeze` sin vectores falsos (2,68 → 2,57); esporas neutral (18,83 = 18,83). Detalle en `docs/PARTICLE_MV.md`. Sigue opt-in.
- Desoclusión: prueba sintética (`tools/disocclusion/synth_disocc.py`): el shader actual ya resuelve la desoclusión con planos exactos (reveal MAE 0,4–1,8 salvo cámara móvil lenta 5,1; mezcla 12–18). Ver `docs/VISUAL_QUALITY.md`.
- Almacenamiento: datos grandes migrados al disco de Steam (ver sección "Almacenamiento" abajo); `tools/ai/ncpaths.py` y `scripts/nc-env.sh` resuelven la ruta (marcador `.verified-<nombre>` tras comprobar la copia).

## Almacenamiento (migración al disco de Steam)

- Disco principal: `sda3` btrfs (/var/home), 445 G, casi lleno. Disco de Steam: `sdc1` ext4 (HDD, distinto disco físico), montado por udisks en `/run/media/system/steam` (915 G, ~32 G libres tras la copia). No se ha tocado steamapps, Steam ni la configuración del disco.
- Raíz de datos: `/run/media/system/steam/NeuralCraft-Data/{datasets,evidence,checkpoints,training,cache,benchmarks}`.
- Copiado con `rsync -a` (originales intactos): `FSR4-neural-data` (46 G, 153 ficheros) y `FSR4-evidence-session{8,9,11,12,13,15}` (≈75 G), más las grabaciones nuevas de la sesión 15 (`evidence/session15-new`).
- Verificación: `rsync -rcn` (checksum completo) sin diferencias en las 7 carpetas. Único caso especial: dos enlaces simbólicos absolutos de session8 (`fgx3_data/b/{frames,planes}`) apuntaban al original; en la copia se han reescrito como relativos. Prueba de escritura/lectura de 64 MB correcta; carga de `animals_b_X.npy` desde el disco nuevo correcta.
- Activación: `tools/ai/ncpaths.py` / `scripts/nc-env.sh` usan la copia si existe `NeuralCraft-Data/.verified-<nombre>`; `NC_DATA_ROOT` lo fuerza. Sin marcador vuelven a `~/Escritorio/<nombre>`.
- Límites: el HDD rinde ~10-15 MB/s en copia, así que el entrenamiento debe cargar datasets con mmap/caché y no regrabar planos allí en masa; el montaje /run/media depende de que el disco esté conectado y montado.
- Originales en el disco principal: no borrados, pendientes de autorización del usuario. Código, Git y `~/neuralvenv` siguen donde estaban.

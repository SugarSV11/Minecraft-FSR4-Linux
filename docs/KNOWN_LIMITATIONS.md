# Limitaciones conocidas

## FSR 4.1.1 sin Wine
- No es posible hoy (ver `docs/NATIVE_FSR4_FEASIBILITY.md`): bloqueado por binarios firmados DX12, ausencia de backend Vulkan oficial, licencia sin ingeniería inversa y falta de FP8 en matrices cooperativas de RADV. Wine/GE-Proton/VKD3D-Proton sigue siendo requisito.


## Sesión 9
- **CRC de planos compartidos: corregido** (orden por inodo; ver STATUS). Si reapareciera un fallo del autotest ya no sería por orden de fd: ejecutar `FSR4_CRC_DIAG=1` y `scripts/crc-stress.sh`.
- **FG ×3 y partículas**: sin vectores propios de partícula el generado equivale a mezclar (PSNR ≈22 dB, 6 % de píxeles con error grosero); no hay mejora medida posible sin datos del motor. Agua en flujo, vegetación y transparencias sí funcionan (cámara móvil +7…+10 dB sobre mezclar).
- **Cámara fija**: FG ×3 genera ≈ lo mismo que mezclar (−0,1…−1,5 dB); no hay ganancia de calidad, solo de fluidez.
- Animales con animación compleja: movimiento rígido por caja (sin huesos); error grosero 0,7 % (cámara móvil) y flicker x0,94.
- Latencia: solo modelo (≈ un intervalo de render + 0,7 ms de generación); no medida extremo a extremo.
- Rendimiento: el nativo (1181 FPS) sigue presentando más que cualquier modo con FG en la escena limitada por CPU; el coste dominante de FG ×3 es el hilo de render (3 presentaciones por fotograma), no la GPU (0,5–0,85 ms el par).
- La carga externa del sistema (compilaciones, compresión de imágenes) hace que el helper de Wine agote el tiempo de espera ("helper timeout") y FSR/FG se desactiven con aviso; se observó una vez con carga 5–7 y no se repitió en reposo.
- Las pruebas de regresión se hacen con el arnés (`TestHarness`); no se probó con un servidor multijugador.


## Sesión 8
- Vectores por entidad: movimiento rígido por caja (inflada 0,3 bloques), sin animación de huesos, máx. 48 entidades, translúcidos sin profundidad no clasificados.
- **FG ×3 es EXPERIMENTAL** (desactivado por defecto): partículas/agua/translúcidos no medidos, UI translúcida aproximada, desoclusión sin inpainting, latencia extremo a extremo no medida (sin hardware), calidad con FSR activo no medida contra nativo verdadero. En la escena CPU-bound medida **no supera los FPS del juego nativo** (852 vs 1163 presentados) y **no alcanza 2000 FPS**.
- Intermitente preexistente: el autotest CRC de los planos compartidos falló 1 de ~12 arranques (FG no se activó; reintento correcto).
- `debugDump` fuera del hilo del juego compite con los fotogramas en vuelo; para pruebas usar `FsrRuntime.dumpNextPrep` / `FSR4_MOTION_PLANES` (volcado síncrono).
- Proxys engañosos: la media de los dos reales vecinos NO es la verdad para objetos en movimiento (llevó a una conclusión errónea sobre la escala Y de FG, retractada); usar siempre el estado verdadero del arnés.


- **Rendimiento**: tras optimizar el puente (fence compartida, sin esperas CPU), el coste restante es el trabajo GPU propio de FSR (4.1.1: ≈0.93 ms @1080p, ≈4.1 ms @4K de salida; 3.1.5: ≈0.5 / ≈2 ms). Con Minecraft vanilla en esta GPU FSR 4.1.1 NUNCA supera al nativo y FSR 3.1.5 solo en 4K+Fabulous+Ultra Performance (+8 %). Ver BENCHMARKS.md.
- La ruta de fence requiere `VK_KHR_external_semaphore(_fd)` en el dispositivo de Minecraft (la añade el mixin); si falta o el import falla se usa automáticamente la ruta de espera CPU (se registra en el log). Si el helper muere con un frame en vuelo, la cola GPU de Minecraft espera la señal: el cliente señala el semáforo desde CPU en los errores detectados en `process`; una muerte del helper *entre* la respuesta y la señal GPU no está cubierta por un watchdog (no reproducida).
- Las tandas de benchmark son escenas estáticas, 3 repeticiones consecutivas por modo dentro de una sesión (sin IC); no hay medición de latencia extremo a extremo con hardware externo (por diseño no hay retardo de frame).
- Exposición: AUTO_EXPOSURE; near=0.05 provoca el aviso "cameraNear very low" del runtime AMD.
- FSR 3.1.4 Vulkan: no implementado. Native AA se ofrece pero 1.0x no fue probado en juego.
- NO hecho: pantalla de ajustes en el juego (sólo F8/F9/F10 y overlay), reasignación de teclas desde menú propio (sí desde Controles de Minecraft), nitidez desde GUI (campo `sharpness` en JSON), benchmarks en escenas limitadas por GPU, pantalla completa/redimensionado en vivo en juego (el controlador lo gestiona; no probado en juego).
- Requiere GE-Proton y los DLL AMD instalados con `scripts/install-runtime.sh`; Linux/AMD (RADV) únicamente.
- Pruebas de rendimiento: `inactivityFpsLimit` debe ser `minimized` (ver BENCHMARKS.md).

## Frame Generation (F7)
- **No aumenta los FPS presentados** en ningún escenario medido (ver BENCHMARKS.md/FRAME_GENERATION.md); reduce los renderizados (~4.5x a 1080p) y añade latencia (+2.8 ms a +8.5 ms medidos por software; sin medición con hardware externo).
- Requiere `amdxcffx64.dll` de AMD en el prefijo de Wine (no redistribuible) para el proveedor 4.0.1.
- El FG interpola la imagen final incluyendo el HUD (sin ruta hudless): elementos de HUD en movimiento pueden mostrar artefactos; los estáticos se mantienen estables.
- Primeros 3 fotogramas tras activar/redimensionar/cambiar de modo no se presentan generados (sin historial). Un teletransporte (>8 bloques) fuerza `reset`.
- Pacing opcional (`fgPacing`) bloquea el hilo de render (la presentación se hace desde el mismo hilo): reduce FPS renderizados un ~12 % y apenas mejora la regularidad (σ 1.0→0.9 ms).
- Con VSync ON el pacing lo impone el swapchain (FIFO) pero es irregular (σ 3.2 ms a 100 Hz).
- "Presentados" = `vkQueuePresent`; el compositor/monitor (100 Hz) muestra como máximo 100 imágenes/s.
- Solo se probó el proveedor 4.0.1 en el juego; 3.1.6 solo en el prototipo sintético.

## Fase de finalización (sesión 5)
- Entidades, partículas y agua en movimiento no tienen vectores de movimiento propios (solo reproyección de cámara): posible ghosting; NO medido ni verificado.
- Los vectores correctos mejoran la nitidez en movimiento lento frente a vectores erróneos, pero el PSNR frente al nativo casi no depende de ellos en escena estática congelada (ver VISUAL_QUALITY.md).
- La sincronización GPU↔GPU con fence depende de `VK_KHR_external_semaphore_fd`; sin ella se usa el camino con espera de CPU (más lento).
- El menú SÍ se probó con clics reales sintéticos (sesión 6, ver TEST_RESULTS.md); no con un ratón físico. Mientras el FG espera a asignarse, `fgStatus` muestra `PENDING (…)` (sesión 7). El supuesto fallo intermitente "FG queda OFF" de la sesión 6 era un error de análisis, no un defecto (ver TEST_RESULTS.md).
- Las comparaciones de posición entre sesiones con `/tick freeze` no son fiables para traslaciones (el servidor congelado ignora correcciones de posición del cliente); las traslaciones se miden en sesiones separadas por modo.

## Sesión 10
- FG ×3/×4 propio sigue EXPERIMENTAL. ×4 solo por `fgMultiplier: 4` en el JSON (no en el menú).
- "Presentados/s" cuenta `vkQueuePresentKHR`; el monitor (100 Hz) muestra ≤100 imágenes/s. Con VSync OFF no hay medida de imágenes visibles (la sonda `present_wait` informa a ritmo de envío en `IMMEDIATE`).
- Con VSync (FIFO) se usa la ruta de un envío por imagen (la de lotes añadía ≈21 ms de cola); FG no sube los FPS mostrados en VSync, solo baja el ritmo de renderizado (100 → 50/33/25).
- Latencia extremo a extremo no medida (sin hardware).
- Hay dos monitores (100 Hz principal, 60 Hz vertical): la ventana de pruebas puede abrirse en cualquiera y las medidas de VSync dependen de ello.
- Validación Vulkan: persisten (preexistentes) `VUID-VkImageCreateInfo-pNext-00990` / `VkImportMemoryWin32HandleInfoKHR-handleType-09861` (imágenes compartidas con el helper por DMA_BUF con tiling OPTIMAL: RADV las acepta, la consulta de formato externo no las declara) y fugas de objetos nativos al cerrar el proceso.
- Una red neuronal pequeña como posprocesado local no mejora los fotogramas generados (ver `docs/AI_RESEARCH.md`).
- Sin vectores por hueso ni por vértice; partículas con vectores = código sin probar.
- Cambios de resolución: durante 1–2 fotogramas de transición no se genera (guardas `fgSizeMatches`).
- `fgx3_test` con `FGX3_TEST_BADMV=1` (control negativo) no falla (tampoco en a27c1f6): el control no discrimina; la prueba válida de vectores es la comparación contra fotogramas verdaderos.
- Vectores de partículas: opt-in, SSIM −0,016 en la escena de partículas, coste ≈4 % en escenas sin partículas, partículas no-quad sin vectores (`docs/PARTICLE_MV.md`).


## Corrector neuronal (sesión 11)
Experimental y NO integrado: no mejora de forma fiable en escenas no vistas (ver NEURAL_EVALUATION.md); el motor Vulkan cuesta 4,8 ms a 1080p (kernel escalar FP32, sin cooperative matrices).
Sesión 12: el corrector aprendido promedia imágenes para bajar el MSE (humo duplicado en partículas); el PSNR sobre la verdad no mide ese ghosting. Parpadeo temporal sin medir. Motor Vulkan sin soporte de 7 candidatos/puerta ni coop-matrix.

- (S13) `FSR4_PARTICLE_MV=1` empeora el generador dentro de las partículas translúcidas (A/B medido); mantener desactivado. Las ganancias de PSNR del corrector neuronal no son mejoras visuales (MAE, error grosero y mezcla de dos imágenes empeoran).

## Sesión 15
- Los datasets/evaluaciones de partículas de las sesiones 10–14 se grabaron a ≈0,6 s por fotograma (≈12 ticks): movimiento 10–20× mayor que en juego real y, con el estimador antiguo, vectores de partícula mal escalados. Las conclusiones neuronales (sbg8, k1–k4) sobre partículas deben leerse con esa reserva; no se reentrenó nada.
- Vectores de partícula: un vector por píxel no representa humo translúcido sobre fondo (prueba sintética: peor que ignorarlo con alfa ≤0,5). Gana en escenas reales, pero sin máscara por alfa del sprite queda un error residual.

## Continuidad 2026-10-22
- Las capturas de humo y esporas a `tick rate 1` de `session15-new` conservan planos A/B y marcas `cam` junto a los PNG (150 por escena). Pueden servir para el siguiente análisis con replay; falta GPU accesible para ejecutarlo aquí. TEST3 continúa sellado.
- Este sandbox no expone `/dev/dri` ni el servidor gráfico al proceso de pruebas; no hay verificación actual de FSR 4.1.1/3.1.5, FG ×2/×3/×4, F6–F10, persistencia o recuperación del helper. Tampoco está disponible el antiguo entorno ROCm `~/neuralvenv/v`.
- La restricción de escritura de `.git` observada en la sesión anterior no se reprodujo el 2026-10-22: montaje btrfs `rw`, propietario con permisos de escritura y creación de archivo temporal con nombre dentro de `.git` correcta. La caché `~/.gradle` también acepta escritura; la compilación Gradle se bloquea al intentar crear un socket de red en el sandbox.
- Desoclusión, IA especializada, coste GPU neuronal y regresión completa en juego: no abordados en la sesión 15.

## Reanudación 2026-10-22
- `/dev/dri` y la RX 9060 XT ya son accesibles en esta sesión; se pasaron Gradle, `prep_test`, `fgx3_test`, `ipc_test` y `gpu_ipc_test`, además de benchmarks en juego de FSR 4.1.1 y FG ×4. Las frases de la auditoría anterior sobre bloqueo por sandbox son históricas.
- El corrector localizado por profundidad sigue **offline y no integrado**: en cámara móvil las reglas ensayadas reducen PSNR/SSIM o aumentan MAE/ghosting. El 17,41 %/46,9 % histórico (17,70 %/47,36 % con la nueva máscara) localiza error, pero no identifica por sí mismo el color correcto. TEST3 permanece sellado; no se entrenó una red nueva.
- FG ×4 conserva limitaciones de nitidez en barridos (proxy de gradiente 0,877–0,885 en dos escenas sintéticas), el monitor vertical muestra como máximo 60 imágenes/s y la latencia extremo a extremo sigue sin medirse con hardware. La variante int64 continúa opt-in pese a su menor tiempo GPU en replay: la diferencia de FPS en juego es pequeña frente a la variación entre sesiones.

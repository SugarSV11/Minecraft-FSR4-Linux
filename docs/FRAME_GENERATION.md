# AMD FSR Frame Generation 4.0.1 en el puente (sesión 4)

## Resumen honesto
- **FSR Frame Generation 4.0.1 oficial funciona dentro de Minecraft 26.3** (proveedor confirmado por el runtime de AMD, fotogramas generados auténticos y presentados). Tecla **F7** (reasignable en Controles), independiente del upscaler (F8/F9/F10 intactos).
- **No se alcanzan 2000 FPS, ni de lejos, ni es alcanzable en esta GPU**: partiendo de ~1120 FPS nativos a 1080p, el mejor resultado con FG fue **502–632 FPS presentados** (251–316 renderizados + el mismo número de generados), es decir, MENOS que sin FG. Causa medida: el propio FG cuesta 1.3–2.0 ms de GPU a 1080p (≈4–6 ms a 4K) y el fotograma nativo completo cuesta 0.89 ms.
- Se mantienen tres magnitudes SEPARADAS: **renderizados** (fotogramas del juego), **generados** (salida del proveedor AMD entregada al swapchain), **presentados** (llamadas `vkQueuePresent`). El monitor (1920x1080 @ 100 Hz) muestra como mucho 100 imágenes/s; no hay forma de medir imágenes realmente mostradas desde el proceso.

## Viabilidad (auditoría y experimentos)
| Experimento | Resultado | Evidencia |
|---|---|---|
| E5-1: carga del DLL `amd_fidelityfx_framegeneration_dx12.dll` (SDK 2.3.0, API FG 4.0.1) bajo GE-Proton11-6 + VKD3D-Proton en la RX 9060 XT | Proveedores ofrecidos: **4.0.1** (`0xf600000081000001`) y 3.1.6 (`0xf600000000c01006`); `CreateContext` rc=0 en ambos; memoria GPU 87 MiB (4.0.1) / 122 MiB (3.1.6) a 1080p | `docs/evidence/E5_1_framegeneration_provider.txt` |
| **Dependencia hallada**: 4.0.1 solo se ofrece si el prefijo de Wine contiene **`amdxcffx64.dll`** (componente de controlador de AMD que descarga el verbo `amdxcffx64` de GE-Proton). Sin él: solo 3.1.6. El upscaler 4.1.1 NO la necesita | reproducido: prefijo del runtime sin el DLL → `count=1` (3.1.6); con él → `count=2` (4.0.1, 3.1.6) | este documento |
| E5-2: prototipo independiente con 2+ fotogramas, MV y profundidad exactos, escena analítica de 3 capas con verdad de referencia en t=k−0.5 | **Interpolación auténtica**: generado vs verdad 40–43 dB (promedio de k−1,k: 23.5 dB; copia de k: 21.9 dB); en regiones con objetos móviles 29–32 dB vs 12.4 dB; equidista de k−1 y k (21.9/21.9 dB). Con el signo de MV invertido: 27.5 dB (peor) → convención FSR (previo−actual) correcta | `docs/evidence/E5_2_fg_dispatch_synthetic_4.0.1.txt` |
| Error de uso corregido en el prototipo | sin `ffxConfigure` por fotograma el proveedor fuerza `reset` (devuelve el fotograma actual, 138 dB vs k): el `frameID` debe coincidir con el último `Configure` y crecer de 1 en 1 | código del SDK `ffx_provider_fsr3framegeneration.cpp` (`bDisjointFrameID`) |
| E5-3: coste GPU de Prepare+FG (sintético, timestamps D3D12) | 4.0.1: 720p ≈1.0 ms; 1080p ≈1.35–2.0 ms; 1440p ≈3.0 ms; 4K ≈4.3–5.9 ms. 3.1.6: 1080p ≈1.1–1.7 ms, 4K ≈4.5–6.0 ms (estados de reloj bimodales) | `docs/evidence/E5_3_fg_gpu_cost_sweep_synthetic.txt` |

## Arquitectura implementada
1. **Helper (Wine)**: `kCmdFgAlloc` crea el contexto FG (flag sin swapchain, sin tocar swapchain DX12) y 2 texturas compartidas (color final y fotograma generado, RGBA8 a tamaño de pantalla; la de salida con UAV); `kCmdFgFrame` hace por fotograma `Configure` + `PrepareV2` (profundidad y MV de la pasada de preparación) + `Dispatch`, de forma asíncrona sobre la fence compartida (espera W, señala S; si falla señala igualmente S). El proveedor realmente creado se devuelve al cliente (nunca se asume).
2. **Biblioteca nativa**: importación dma-buf de los 2 planos con autoverificación CRC (6 planos en total), copias con barreras, API C `fsr4_fg_*`, IPC dividida en `post`/`collect` para que el hilo de render no espere la grabación CPU del helper.
3. **Mod**: `FsrRuntime.presentHook` (mixin `MinecraftMixin`, antes del blit de Minecraft): copia el fotograma final (con HUD) → helper; copia el generado de vuelta → `blitFromTexture` + `present` del generado, `acquireNextTexture`, y a continuación Minecraft presenta el real. Modos: **con upscaler** (reutiliza las MV/profundidad del upscaler) y **solo FG** (render nativo + pasada de preparación). Pacing opcional (`fgPacing`, apagado por defecto: bloquea el hilo de render y reduce los FPS).
4. **F7**: activa/desactiva (persistido en `config/fsr4linux.json`: `frameGeneration`, `fgPacing`); "ACTIVO" solo se notifica cuando el runtime de AMD confirma el proveedor 4.0.1; si falla (DLL ausente, error de GPU) se desactiva, se explica por qué y se restaura el estado.
5. Fallback seguro: cualquier fallo del helper deja la cola de Minecraft sin bloqueo (señal de S desde el helper o desde el cliente).

## Medidas en el juego (1080p salvo indicación; 3 repeticiones; VSync OFF; sin otras cargas)
Ver `docs/BENCHMARKS.md` sección "Frame Generation" y `docs/evidence/fg/`.

## Cómo habilitarlo
`scripts/install-runtime.sh` instala el DLL de FG del SDK. Para 4.0.1 hay que tener `amdxcffx64.dll` en el prefijo (`~/.local/share/fsr4linux/prefix/pfx/drive_c/windows/system32/`): lo descarga GE-Proton con su verbo `amdxcffx64` (o `FSR4_COPY_AMDXCFFX=/ruta/al/amdxcffx64.dll scripts/install-runtime.sh` para copiarlo desde otro prefijo local). **No se redistribuye** (licencia propia de AMD).

## HUD separado (sesión 5)
Antes FG interpolaba la imagen final con HUD. Ahora usa el mecanismo oficial `HUDLessColor`: el mundo ya reescalado (incluida la mano), ANTES de dibujar el HUD, se copia a un tercer plano compartido (plano 6); el proveedor extrae la UI comparando ambos y la recompone sobre el fotograma generado. Medido en el juego (giro continuo): diferencia media del HUD entre fotograma real y generado — barra de objetos **0.04–0.05** (antes 0.32–0.69), notificación (toast) **0.00** (antes 0.3–0.9). `FSR4_FG_NO_HUDLESS=1` restaura el comportamiento anterior. La cruceta (mezcla invertida) queda dentro del ruido del mundo.

## Recuperación de errores (verificada en el juego)
| Fallo inyectado | Resultado |
|---|---|
| `kill -9` del helper de Wine con FSR 4.1.1 + FG activos | detectado en el fotograma siguiente; FSR y FG se desactivan con aviso ("el proceso helper de Wine ha terminado"), el juego sigue en nativo; F10 reinicia el puente y reactiva FSR, F7 reactiva FG (benchmark posterior 220 FPS = igual que antes) |
| `kill -STOP` del helper (congelado) | timeout de 2 s detectado, FSR y FG desactivados con aviso, helper congelado eliminado y reiniciado limpio al reintentar; sin cuelgue |
| Defecto encontrado y corregido: tras morir el helper por la ruta de error del frame, el puente se reiniciaba REUTILIZANDO el semáforo compartido obsoleto → cola de la GPU esperando una señal inexistente (bloqueo infinito en `drm_syncobj_wait` dentro de `vkQueuePresent`) | el semáforo se destruye al reiniciar el puente (`fsr4_bridge_start`/`stop`), el fallo mata el helper y limpia, y un **hilo vigilante** libera la GPU (señala el último valor emitido) si hay una espera pendiente y ningún fotograma ha empezado en 3 s; no se disparó en ninguna ejecución normal |

## Sesión 7: desglose del coste (ver PERFORMANCE_OPTIMIZATIONS.md §4)
Solo FG a 1080p: dispatch FG 1.43–1.48 ms de GPU; cadena completa 1.6 ms en la cola; IPC 0.01 ms; entrada/salida 0.02 ms cada una; el hook de FG gasta 1.31 ms de CPU, de los cuales 1.15 ms están dentro de `submit()` de Minecraft (espera de envíos en vuelo, `MAX_SUBMITS_IN_FLIGHT = 2`), y la cola GPU queda ociosa 2.5–2.6 ms por fotograma renderizado. FSR 4.1.1 + FG: hueco 3.3–3.5 ms. El modelo oficial no se modificó; ×3/×4: solo el estudio offline de `docs/EXPERIMENTAL_BACKENDS.md` (no integrado).

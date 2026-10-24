# Resultados de pruebas (2026-10-11)

## Sesión 9
| Prueba | Resultado |
|---|---|
| `crc_stress restart 60` con todos los núcleos al 100 %, ANTES de la corrección | 4 fallos de 60 (permutación de planos 0–2) |
| `crc_stress restart 80` + `realloc 300`, con carga, DESPUÉS | 0 fallos / 0 fallos |
| `FSR4_CRC_SHUFFLE=1 crc_stress same 10`: con ordenación por inodo / con `FSR4_CRC_NOSORT=1` | PASS / FAIL (misma firma de CRC que el fallo natural) |
| `fgx3_test` (ahora 5 casos: + UI oscura translúcida α=0,5, umbral 50 dB; control negativo `FGX3_TEST_BADMV=1` falla) | PASS (53,1 dB en el caso nuevo; 44,3/48,9 dB con el método aditivo anterior) |
| Escenas reproducibles (7 escenas × 49 casos): `scripts/x3-eval-all.sh` | `docs/evidence/session9/scene_eval_*.txt` |
| 23 tests Java, `prep_test`, `ipc_test`/`gpu_ipc_test` 4.1.1 y 3.1.5 | PASS |
| `scripts/regression-x3.sh`: F7–F10, calidad/versión, pantalla completa 1920x1080, menú + clic en el selector (×3 ⇄ ×2 AMD confirmado por el runtime), Nether ⇄ Overworld, salir/entrar al mundo, `killhelper` (desactiva con aviso) y recuperación con F10+F7, persistencia de `fgMultiplier` | PASS |
| Una primera ejecución de la regresión se abortó con "helper timeout" bajo carga externa (5–7) y una segunda quedó colgada porque el arnés esperaba una pantalla de título que no llegaba tras `disconnectWithProgressScreen` (error del arnés, corregido con `disconnect(TitleScreen)`); repetida en reposo: PASS | – |


## Sesión 8: vectores de movimiento y FG ×3
| Prueba | Resultado |
|---|---|
| 23 tests Java (`--rerun-tasks`; incluye `fgMultiplier`: persistencia y valores inválidos → 2) | PASS |
| `prep_test` (convención Y de plano abajo-arriba, 3 casos de entidad: traslación+guiñada con/sin cámara móvil, caja fuera del plano) | 7/7, error máx. 0,028 px |
| `fgx3_test` (textura trasladada + bloque de HUD, primer plano con desoclusión; PSNR ≥ 38 dB fondo / ≥ 28 dB total, HUD intacto) | PASS (52,9 dB; 42,9–53,0 dB con desoclusión); control negativo con MV=0 → FAIL como debe |
| `ipc_test` 4.1.1 / 3.1.5, `gpu_ipc_test` 4.1.1 / 3.1.5 | PASS |
| En juego, FSR 4.1.1 + FG ×3: F7×2, F8×2, F9×2, F10×2, giro continuo, pantalla completa ON/OFF, menú, clic real en el selector (x3 ⇄ x2 AMD, `fgProvider=4.0.1` confirmado por el runtime), `killhelper` | sin cuelgues; el helper muerto desactiva FSR/FG con aviso; `fgMultiplier=3` persistido |
| En juego, configuración por defecto (FSR 4.1.1 + FG ×2 AMD) tras los cambios: mismo estrés | PASS, sin errores |
| Calidad: A/B de vectores (2 repeticiones), FG ×2 y ×3 contra fotogramas verdaderos, prototipo ×3 (10 casos) | `docs/evidence/session8/*.txt`, `docs/VISUAL_QUALITY.md`, `docs/FG_X3.md` |


## Revalidación tras apagón (2026-10-12)
22 tests Java (rerun), prep_test, ipc_test (4.1.1, 3.1.5), gpu_ipc_test (4.1.1, 3.1.5): PASS. En el juego: `provider 4.1.1` del runtime AMD, FG `ACTIVE` proveedor 4.0.1, F7×2, F8×2, F9×2, F10×2 sin errores (`fabric/run/fsr4-test.log`).


## En Minecraft real (perfil aislado fabric/run, Vulkan RADV)
| Prueba | Resultado | Evidencia |
|---|---|---|
| Timeout previo (exit 124) | NO reproducible: 8 ejecuciones completas con exit 0 (~30–60 s). Con SkyRendererMixin el cielo se dibuja bien. Causa más probable: límite de tiempo del harness. No confirmada. | `ingame/native.png`, `fsr.png` |
| FSR 3.1.5 (toggle F10) | OK, `executed=3.1.5` | `ingame/fsr.png` |
| **FSR 4.1.1 (F8)** | **OK**: contexto creado por el runtime AMD (`provider 4.1.1 id=0xf5a5ca1e01001001`), dispatches reales cada fotograma, resultado presentado, HUD nativo | `ingame/fsr411.png`, `run_fsr411_f8.log`, bench CSV |
| Cambio de calidad en caliente (F9) | OK con 4.1.1 y 3.1.5 (realoca, sin cierres) | log |
| Cambio de versión en caliente (F8) | OK 3.1.5 <-> 4.1.1 | log |
| Persistencia de configuración | OK (`fabric/run/config/fsr4linux.json`) | |
| Movimiento de cámara | Cualitativo: sin imágenes dobles/estelas visibles tras giros (`mot_a.png`, `still_after.png`). Sin comparación numérica. | |

## Bug corregido
FSR 4.1.1 devolvía `ffxDispatch rc=6` en el juego porque el contexto se creaba sin `AUTO_EXPOSURE` y sin recurso de exposición (el prototipo sí lo usaba; FSR 3.1.5 lo tolera). Ahora se pasa el flag. FSR se desactivó con restauración limpia (probado el camino de fallo).
También: tras `CLIENT_STOPPING` el controlador seguía procesando redimensionados con el puente parado (spam de errores al cerrar); corregido.

## Sin cambios respecto a STATUS anterior
Tests unitarios (14), prep_test, ipc_test, gpu_ipc_test: ver docs/evidence.

## Sesión 3 — optimización del puente
| Prueba | Resultado |
|---|---|
| E4 fence compartida D3D12↔Vulkan | PASS (docs/evidence/E4_shared_fence_timeline.txt) |
| Equivalencia visual fence vs espera-CPU (misma sesión, PSNR vs nativo 29.45 dB vs 29.61 dB; fence vs CPU 45.9 dB, ruido de base nativo-nativo 48.1 dB) | OK, sin corrupción ni teselado (`ingame/fence2_*.png`, `cpu2_*.png`) |
| Dispatch directo vs con copias (PSNR 45.7 dB; 0.11 % de píxeles con diff>16, bordes de follaje) | OK (`ingame/direct{0,1}_still.png`) |
| Estrés con cambios en vivo (F8 x2, F9 x3, F10 x2, giro continuo, pantalla completa ON/OFF) con la ruta final | sin cuelgues, sin errores del helper (0 `status` de error en bridge.log), 4 reinicializaciones GPU todas con fence; HUD nítido (`ingame/stress_fs.png`) |
| Bug hallado y corregido: tras desactivar/activar FSR la fence no se reimportaba ("no new syncobj fd": los números de fd se reutilizan) | corregido: fence persistente durante la vida del helper |
| tests nativos tras cambio de protocolo (v2): prep_test, ipc_test 4.1.1 (incl. crash/restart), gpu_ipc_test 4.1.1 (incl. resize) | PASS |
| tests unitarios Java (14) + compilación | PASS |
| Ghosting/flicker | revisión visual de capturas con movimiento; sin estelas dobles. Sin medición objetiva (sin PSNR temporal) |

## Sesión 4 — Frame Generation
| Prueba | Resultado |
|---|---|
| E5-1 proveedor FG bajo GE-Proton/VKD3D-Proton en RX 9060 XT | PASS (4.0.1 y 3.1.6; 4.0.1 requiere amdxcffx64.dll) |
| E5-2 interpolación con verdad analítica | PASS: 40–43 dB vs 23.5 dB del promedio; MV invertidas empeoran (27.5 dB) |
| tests nativos: gpu_ipc_test `fg` (planos 4/5 importados + CRC, reset reproduce la entrada exacta, siguientes frames interpolados) | PASS |
| ipc_test 4.1.1, prep_test, gpu_ipc_test (seq/latency/resize) con protocolo v3 | PASS |
| Java: 14 tests unitarios + compilación | PASS |
| En el juego: FG con upscaler, FG solo, cambio en vivo F7/F8/F9/F10 + giro continuo (estrés) | sin errores/cuelgues; FG se reasigna tras cada cambio del upscaler |
| Calidad en el juego (`shotpair`, mismo tick): estático 56 dB generado vs real; giro 60°/s 27–29 dB, diferencias solo en bordes, HUD (barra) diff medio 0.83 | sin duplicados/desgarros visibles (`docs/evidence/fg/`) |
| Pendiente: latencia extremo a extremo con hardware, ghosting cuantificado, partículas/agua con FG | no medido |

## Sesión 5 — finalización
| Prueba | Resultado |
|---|---|
| Unitarios Java (22: controlador 14, configuración 5, métricas FG 3) | 22/22 PASS |
| prep_test (incluye anillo de uniformes), ipc_test 4.1.1 y 3.1.5 (seq, reset, resize, negative, crash, latency), gpu_ipc_test 4.1.1 (seq, latency, fg, resize) y 3.1.5 | todos PASS |
| Estrés de integración con el código final: F7×6, F8×2, F9×4, F10×4, giro continuo, pantalla completa ON/OFF, menú abierto/cerrado | sin errores ni avisos; FG se reasigna tras cada cambio del upscaler (9 activaciones) |
| Persistencia: arranque con `enabled/backend/quality/frameGeneration/sharpness/overlay/fps/pacing` guardados | restaurado (FSR 4.1.1 Balanced + FG activo, confirmado por el runtime AMD); F9 y F7 en vivo guardados |
| Recuperación: SIGKILL y SIGSTOP del helper con FSR+FG | recuperado (ver FRAME_GENERATION.md); defecto del semáforo obsoleto corregido |
| Calidad: 16 combinaciones de jitter, controles de MV, volcado de profundidad/MV, comparación determinista | ver VISUAL_QUALITY.md |
| HUD con FG: diferencia real/generado barra 0.04, toast 0.00 | PASS |
| Menú: captura con datos en vivo | verificado visualmente (`docs/evidence/quality/settings_menu.png`) |
| Rendimiento: FSR 4.1.1 Quality 1080p 545 FPS; FG+FSR 213–220; FG solo 229–251 | sin regresión |

## Sesión 6 (2026-10-12, rama experimental/ai-performance): menú con clics reales y estabilidad
Arnés `TestHarness`: nuevos pasos `widgets` (lista los widgets de la pantalla) y `click <índice|texto>` (pulsación y liberación reales vía `Screen.mouseClicked/mouseReleased`, no llamando a la lambda del botón).
Nota técnica medida: en este Minecraft `AbstractWidget.isValidClickButton` solo acepta `MouseButtonInfo(1, 0)` (el valor 0 es rechazado); el arnés usa 1.
Resultados (FSR 4.1.1 + runtime AMD confirmando proveedor en cada estado):
- Reescalado (F10): SÍ→NO→SÍ, el estado `active` sigue al botón. PASS.
- Versión: 4.1.1→3.1.5→4.1.1, proveedor confirmado por el runtime en cada paso. PASS.
- Calidad: QUALITY→BALANCED→PERFORMANCE (resolución interna 853x480→753x424→640x360). PASS.
- Frame Generation: ON → `ACTIVE (with upscaling)`, proveedor 4.0.1; OFF. PASS (3 secuencias distintas).
- Pacing FG / Overlay / FPS: alternan y se persisten en `config/fsr4linux.json`. PASS. Hecho cierra la pantalla. PASS.
- ~~Una ejecución mostró `fg=OFF` tras pulsar FG~~ **RETIRADO (sesión 7)**: era un artefacto de mi análisis (un `grep -o` con patrón `[A-Z_a-z ]*` descartaba la línea `fg=ACTIVE (with upscaling)` por los paréntesis y solo se veían las líneas OFF). Repetida la secuencia completa 3 veces con la salida íntegra: FG pasa a `ACTIVE (with upscaling)` (proveedor 4.0.1) tras el clic en las 3. No hay fallo intermitente reproducido. Se añadió igualmente el estado `PENDING (…)` a `fgStatus` mientras FG espera un límite de fotograma o el reescalado del upscaler.
- Dimensiones: Overworld→Nether→End→Overworld con FSR 4.1.1 + FG activos: sin errores, estado estable. PASS.
- Morir y reaparecer (clic real en "Respawn"): FSR+FG siguen activos. PASS.
- Pantalla completa ON/OFF: salida 1280x720→1920x1080→1280x720, escalador y FG se reasignan. PASS.
- No probado en esta sesión: Alt-Tab real y cambio de resolución por el gestor de ventanas (requieren interacción de escritorio).


## Sesión 7 (2026-10-12)
| Prueba | Resultado |
|---|---|
| Java: 22 tests unitarios (`gradle test --rerun-tasks`) | PASS (22/22) |
| prep_test | PASS |
| ipc_test 4.1.1 y 3.1.5 (seq reset resize negative crash latency) | PASS (0 checks fallidos) |
| gpu_ipc_test 4.1.1 (seq latency fg resize) y 3.1.5 (seq latency resize) | PASS |
| En juego: helper SIGKILL con FSR+FG activos → vuelta a nativo con aviso; F10 reinicia el helper y FSR 4.1.1 vuelve (proveedor confirmado por el runtime) | PASS |
| En juego: helper SIGSTOP → el vigilante lo detecta y vuelve a nativo; tras SIGCONT+SIGKILL, F10 restaura FSR 4.1.1 (≈3–4 s) | PASS |
| F7/F8/F9/F10 con helper recién reiniciado, FG ↔ solo FG, 3.1.5 ↔ 4.1.1 | PASS |
Nota: los binarios de prueba de `build/` (11 oct) eran de un protocolo anterior; los de `build/cmake/` son los vigentes. Con `FSR4_GPU_TS=1` también se ejecutó la ruta de marcas GPU dentro del juego sin errores.

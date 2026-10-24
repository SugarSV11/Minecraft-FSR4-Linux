# Minecraft FSR 4.1.1 Beta 1 — estado

## Ampliación Linux Radeon (rama `compat/rdna1-4`)

La rama de compatibilidad deriva de `beta/1.0`. Detecta las capacidades del dispositivo Vulkan seleccionado, prioriza FSR 4.1.1 en Radeon dedicadas cuando el SDK lo ofrece y vuelve a FSR 3.1.5 si el proveedor no se inicializa o no ejecuta correctamente. La versión mostrada como «en ejecución» exige confirmación del SDK. Frame Generation se comprueba por separado y el menú omite modos incompatibles o fallidos. Solo se admiten Linux y GPU AMD Radeon. La matriz, lo verificado físicamente y lo pendiente están en `GPU_COMPATIBILITY.md`.

Artefacto de esta rama: `dist/minecraft-fsr4-1.0.0-beta.1-compat-rdna1-4/`. No contiene DLLs AMD, IA neuronal, datasets ni pesos. El estado histórico de `beta/1.0` y sus pruebas originales se documenta a continuación.

Verificación de la ampliación: `./gradlew --offline --no-daemon build` pasó **31 pruebas Java**. En la RX 9060 XT, el perfil aislado `fabric/run-compat` confirmó en juego FSR 4.1.1, FSR 3.1.5, AMD FG ×2 y FG propio ×3/×4; el JAR **empaquetado** cargó desde un classpath sin clases de desarrollo del mod en `fabric/run-compat-jar` y confirmó 4.1.1 ejecutado. Los dos perfiles son locales y están excluidos de Git. Ninguna otra GPU se declara verificada.

Rama de estabilización: `beta/1.0`, separada por worktree de `experimental/ai-performance`. Versión Fabric: `1.0.0-beta.1`, Minecraft 26.3, Java 25, Fabric Loader 0.19.5 y Fabric API 0.162.0+26.3. La rama experimental y su árbol de trabajo se conservan; no se ha hecho push.

## Funciones incluidas

- Reescaladores oficiales AMD FSR 4.1.1 y 3.1.5 mediante el helper Wine/GE-Proton y el SDK FidelityFX local.
- AMD Frame Generation ×2; generador Vulkan propio ×3 y ×4. F6 ofrece el multiplicador 2/3/4; F7 conmuta FG, F8 versión, F9 calidad, F10 reescalado.
- Menú de vídeo, persistencia en `config/fsr4linux.json`, indicación del proveedor ejecutado y recuperación tras fallo o bloqueo del helper.
- Liberación de recursos Vulkan antes de que Minecraft cierre el dispositivo.

Los multiplicadores describen imágenes **presentadas**, no una promesa de mejora de rendimiento. FG propio puede mostrar artefactos en partículas, transparencia o desoclusión; véanse `FG_X3.md`, `FG_X4.md` y `KNOWN_LIMITATIONS.md` en el repositorio.

## Paquete e instalación aislada

Ejecutar `scripts/build-native.sh`, luego `(cd fabric && ./gradlew --offline --no-daemon build)`, y `scripts/package-beta1.sh`. La distribución `dist/minecraft-fsr4-1.0.0-beta.1/` contiene el JAR instalable, `libfsr4linux.so` y `fsr4_bridge.exe` propios, la licencia MIT, este documento y sumas SHA-256. El código nativo de inferencia neuronal queda fuera del enlace; JAR y distribución excluyen datasets, pesos, checkpoints, planos, capturas, scripts y shaders neuronales.

Para usarla: instalar el JAR en `mods/` de un perfil **nuevo** de Minecraft 26.3 con Fabric Loader, Fabric API y Java 25. Copiar `runtime/` a un directorio privado y definir `FSR4_RUNTIME_DIR` con esa ruta. Instalar allí, desde una descarga legítima del FidelityFX SDK v2.3.0, `amd_fidelityfx_loader_dx12.dll`, `amd_fidelityfx_upscaler_dx12.dll` y `amd_fidelityfx_framegeneration_dx12.dll` en `bridge/`; conservar el aviso `docs/license.md` de AMD. Configurar GE-Proton con `FSR4_PROTON` si no está en la ruta estándar. Para AMD FG 4.0.1 se requiere además `amdxcffx64.dll` obtenido del propio entorno AMD/Proton en `prefix/pfx/drive_c/windows/system32/`; el paquete no lo distribuye. `scripts/install-runtime.sh` hace una copia **local** desde el SDK existente y admite `FSR4_RUNTIME_DIR`; no usar su valor por defecto para probar la beta aislada.

El documento `~/.cache/fsr4-linux/FidelityFX-SDK/docs/license.md` permite redistribuir en binario los archivos cubiertos si se reproducen sus avisos; no se ha verificado permiso de redistribución de `amdxcffx64.dll`, por lo que la distribución no contiene DLLs de AMD ni GE-Proton. La biblioteca del mod depende de `libstdc++`, `libgcc_s`, `libm` y `libc` del sistema; también necesita Vulkan/RADV y Wine/GE-Proton en ejecución.

## Verificación

- Gradle `build`: 23 tests Java pasados; compilación del JAR y del helper finalizada.
- `prep_test` y `fgx3_test`: pasados en RADV RX 9060 XT. `fabric/run` (Java 25, Vulkan) confirmó proveedor FSR 4.1.1 ejecutado, AMD FG 4.0.1 ×2 activo, cambio a FSR 3.1.5, calidad y F10; F6 cicla 2→3→4 y ×3/×4 se activaron en juego. Tras reiniciar persistieron FSR 3.1.5 Balanced y FG ×4.
- En el mismo perfil, se terminó el helper durante FSR 4.1.1: el mod lo desactivó, informó el error y F10 reactivó FSR 4.1.1 tras iniciar de nuevo el puente. El juego cerró sin fallo y liberó recursos Vulkan antes del dispositivo.
- El **JAR empaquetado**, cargado desde el classpath sin clases de desarrollo del mod, arrancó en `fabric/run-jar` y confirmó FSR 4.1.1 ejecutado en un mundo de prueba nuevo. Se usó exclusivamente `build/runtime-isolated`; no se tocó `~/.minecraft` ni el perfil principal. Para repetir la prueba de desarrollo, generar `fabric/build/run-classpath.txt` con `(cd fabric && ./gradlew --offline --no-daemon -q printRunClasspath | sed -n 's/^CLASSPATH=//p' > build/run-classpath.txt)` y activar `preferredGraphicsBackend:"vulkan"` y `onboardAccessibility:false` en el perfil nuevo.
- Se inspeccionaron el JAR, la tabla de símbolos de `libfsr4linux.so`, cadenas de los tres binarios y el directorio distribuible: sin archivos neuronales, datasets, pesos ni rutas privadas Steam. `SHA256SUMS` verificó los tres binarios. La copia local para probar AMD contiene DLLs, pero **no** forma parte de `dist/`.
- La rama `experimental/ai-performance` conserva su HEAD `8b26e6e` y sus cambios sin commit; resguardo previo en `../beta1-prebranch-safeguard-20261023-uReV2a/`.

Límites: se verificó funcionalidad en el perfil aislado, no la calidad visual en todas las escenas ni mejoras de FPS. El primer arranque del perfil nuevo eligió OpenGL y mostró el panel inicial de accesibilidad; se configuró Vulkan y se cerró ese panel solo en el perfil beta antes de las pruebas anteriores. No se hizo push.

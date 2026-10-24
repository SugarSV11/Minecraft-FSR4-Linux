# Compatibilidad GPU de Beta 1 en Linux

Rama: `compat/rdna1-4`, derivada de `beta/1.0`. Minecraft 26.3, Vulkan, Fabric. Reescaladores permitidos: **AMD FSR Upscaling 4.1.1** y **AMD FSR 3.1.5**. Frame Generation es independiente: AMD FG ×2 y generador propio ×3/×4. No se añaden otras versiones de reescalado.

## Matriz objetivo y estado real

| GPU | Selección objetivo | FG ×2 AMD | FG ×3/×4 propio | Prueba física de esta rama |
| --- | --- | --- | --- | --- |
| RDNA 4, RX 9000 dedicada | 4.1.1; 3.1.5 si falla | Candidato, sujeto a inicialización y generación reales | Candidatos, sujetos a asignación y generación reales | **RX 9060 XT verificada**; demás RX 9000 pendientes |
| RDNA 3, RX 7000 dedicada | 4.1.1 si lo acepta el SDK; si no, 3.1.5 | Pendiente; el SDK de FG puede rechazarlo | Pendiente | Pendiente |
| RDNA 3.5 integrada | 3.1.5 | No ofrecido por la política del mod | Pendiente | Pendiente |
| RDNA 2 | 3.1.5 | Pendiente; el SDK de FG puede rechazarlo | Pendiente | Pendiente |
| RDNA 1 | 3.1.5 | Pendiente; el SDK de FG puede rechazarlo | Pendiente | Pendiente |

La [documentación oficial de AMD para FSR Upscaling](https://gpuopen.com/amd-fsr-upscaling/) enumera RX 9000 y RX 7000 **dedicadas** para FSR 4, y GPU AMD anteriores para FSR 3.1.5. Su [documentación del SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/Kits/FidelityFX/docs/techniques/super-resolution-ml.md) confirma el requisito de GPU dedicada. Estos son requisitos del proveedor, **no** una certificación de este puente Linux. La [documentación de AMD FG](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/blob/main/Kits/FidelityFX/docs/techniques/frame-interpolation-ml.md) indica requisitos propios, diferentes de los del reescalador; la presencia de FSR 3.1.5 o 4.1.1 no demuestra FG ×2.

## Cómo decide el mod

1. Consulta el **dispositivo Vulkan realmente seleccionado por Minecraft**: `vendorID=0x1002`, tipo integrado/dedicado, Vulkan 1.3 o superior, extensiones de memoria externa dma-buf y los formatos sampled/storage necesarios. También lee el identificador técnico GFX que RADV incluye en el nombre del dispositivo: GFX10 (RDNA 1/2) y GFX1150 (RDNA 3.5) quedan limitados a 3.1.5; GFX1100–1149 y GFX1200–1299 permiten probar 4.1.1 en GPU dedicada. Si el controlador omite GFX, decide el SDK por inicialización y ejecución reales. El nombre comercial y el identificador PCI se registran para diagnóstico, pero no deciden por sí solos. Fuera de Linux o con otro fabricante, el puente queda desactivado.
2. El helper consulta las versiones que ofrece el SDK AMD. Una RX dedicada con GFX elegible intenta 4.1.1 primero; si no se ofrece, intenta 3.1.5. Una GPU integrada solo intenta 3.1.5. En una GPU antigua o sin GFX conocido, la mera enumeración de 4.1.1 **no** se toma como prueba de soporte: el SDK debe crear el contexto y ejecutar el primer cuadro; si lo rechaza o reporta otro proveedor, el mod conmuta a 3.1.5. También puede seleccionarse 3.1.5 manualmente con F8.
3. El rótulo de versión **en ejecución** aparece solo después de que el SDK haya reportado exactamente el proveedor pedido durante un cuadro completado. Un proveedor diferente provoca error y fallback; jamás se rotula 4.1.1 si realmente ejecutó 3.1.5. Errores del helper y tiempos de espera desactivan FSR de forma segura, sin repetir un intento inseguro de proveedor.
4. FG tiene comprobaciones independientes. ×2 necesita GPU dedicada y contexto AMD FG 4.0.1 creado por el SDK; ×3/×4 necesitan el puente y la asignación/generación Vulkan propia. El menú salta los modos imposibles o que ya fallaron en la sesión. Si falla un modo en uso, se desactiva FG y Minecraft continúa.

## GPU y funciones verificadas

- **AMD Radeon RX 9060 XT (RADV GFX1200, PCI 0x7590)**, Mesa RADV 26.2.4, Vulkan 1.4, GE-Proton11-6-x86_64, Minecraft 26.3 en `fabric/run-compat` y runtime privado `build/runtime-isolated`: selección automática de 4.1.1 confirmada por el SDK; F8 activó 3.1.5 y confirmó su proveedor. AMD FG ×2 reportó `4.0.1 *` y quedó activo. FG propio ×3 y ×4 quedaron activos en modo FG sin reescalado. Las dos sesiones terminaron normalmente; registros en `fabric/run-compat/fsr4-test.log` (perfil de prueba, excluido de Git y del paquete).
- Ninguna RX 7000, RDNA 3.5, RDNA 2 ni RDNA 1 se probó físicamente en esta rama. Su funcionamiento en Linux, calidad de imagen y estabilidad quedan **pendientes de validación**. En particular, FG ×2 en GPU anteriores a RDNA 4 no se considera compatible hasta que el SDK lo inicialice y genere cuadros reales.

## Próximas comprobaciones

Usar un perfil y mundo de prueba nuevos por tarjeta, sin tocar mundos ni instalaciones personales. Anotar fabricante/tipo/API/extensiones del dispositivo Vulkan seleccionado, versiones enumeradas, retorno de `ffxCreateContext`, versión de `GET_PROVIDER_VERSION` y resultado de `ffxDispatch` para cada reescalador. Probar falla y recuperación del helper, cambio F8, FG ×2/×3/×4 por separado, persistencia, cierre y resolución variable. Registrar los resultados de cada GPU aquí sin inferirlos de otra familia.

El paquete distribuye solo código propio. Las DLL del SDK AMD y GE-Proton se instalan localmente conforme a sus licencias, como indica `BETA1_STATUS.md`. Los datasets, pesos y checkpoints neuronales siguen reservados para Beta 2; ubicación y continuidad en `NEURAL_BETA2_CONTINUATION.md`.

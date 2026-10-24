# Viabilidad de FSR 4.1.1 en Minecraft nativo Linux/Vulkan

Fecha: 2026-10-11. Estados: SUPPORTED / EXPERIMENTAL / BLOCKED / UNVERIFIED.

## Hechos verificados (con fuente)
| Hecho | Fuente |
|---|---|
| FSR SDK 2.3 (junio 2026, tag `v2.3.0` publicado 2026-06-24) incluye FSR Upscaling **4.1.1**, Frame Generation 4.0.1, Ray Regeneration 1.2.0, Radiance Caching 0.9.0. Upscaling 4.1.1 añade RX 7000. | <https://gpuopen.com/amd-fsr-sdk/>, <https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/releases> |
| Sistemas operativos listados: Windows 10/11. API gráfica listada: DirectX 12 (+ plugin UE5). Linux no aparece. | GPUOpen FSR SDK |
| 2.3 exige usar `amd_fidelityfx_loader_dx12.dll` y "pre-built, signed AMD FSR API DLLs". | GPUOpen FSR SDK |
| README del repo: "Vulkan is currently not supported in SDK" (tabla de problemas conocidos). El manual 2.3 aún dice "not supported in SDK 2.2" (texto desfasado, pero sin ninguna indicación de lo contrario). | <https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK>, <https://gpuopen.com/manuals/fsr_sdk/> |
| **Comprobado en el código del tag 2.3.0**: `Kits/FidelityFX/backend/` solo contiene `dx12`; las cabeceras de API solo tienen `dx12/`; `signedbin/` solo trae `*_dx12.dll`. `FFX_API_BACKEND_ID_VK` está definido en `ffx_api.h` como reserva sin implementación. Los DLL son PE32+ x86-64 de Windows; el upscaler importa `dxgi.dll` y carga `amdxc64.dll`. | clon local del repo, `objdump -p`, `strings` |
| Requiere Shader Model 6.6 (CS_6_6); GPUs RX 7000/9000. | <https://gpuopen.com/manuals/fsr_sdk/techniques/super-resolution-ml/> |
| Documentación de la técnica: entradas color (render res), profundidad, vectores de movimiento, exposición opcional; jitter Halton[2,3] con 18/23/32/72 fases (Quality/Balanced/Performance/UltraPerf); ratios 1.0/1.5/1.7/2.0/3.0; estados DX12 `NON_PIXEL_SHADER_RESOURCE`. Sin ninguna mención a Vulkan. | misma página |
| Licencia: `docs/license.md` del SDK. Licencia general restrictiva ("binary form only", sin ingeniería inversa) **excepto** una lista de ficheros bajo licencia estilo MIT que incluye `Kits\FidelityFX\signedbin\amd_fidelityfx_{upscaler,loader,framegeneration,denoiser,radiancecache}_dx12.dll` y las cabeceras públicas de API. Permite redistribuir/modificar esos ficheros con el aviso de copyright. | `docs/license.md` líneas 570-586 y 890 |
| `amdxcffx64.dll` (DLL de AMD descargable de download.amd.com, enviada por Proton en `contrib/`) **no** está cubierta por la licencia anterior y su licencia no se ha localizado → **no se redistribuye**; solo se usa la copia ya presente en la instalación Proton del usuario. | GE-Proton `amdxcffx64.verb`; ver E2 |
| VKD3D-Proton 3.0 añadió FSR4 mediante `VK_KHR_cooperative_matrix` + `VK_KHR_shader_float8` (solo se expone con float8 nativo, es decir RDNA 4 o superior); hay camino de emulación int8/fp16 solo compilando desde fuente. | <https://github.com/HansKristian-Work/vkd3d-proton/releases> |
| Este sistema expone ambas extensiones en RADV (Mesa 26.2.4). | `docs/HARDWARE_AUDIT.md` |

## Clasificación de rutas
| # | Ruta | Estado | Razón |
|---|---|---|---|
| A | FSR 4.1.1 oficial **nativo Linux/Vulkan** (cargar el escalador oficial desde Minecraft con el dispositivo Vulkan existente) | **BLOCKED** | No hay backend ni DLL/`.so` Vulkan en el SDK 2.3.0 (verificado en código). Los binarios firmados son PE de Windows y solo DX12. No existe versión posterior que lo cambie: `v2.3.0` es el último release (2026-06-24). |
| B | Ejecutar el DLL oficial bajo **Wine/Proton + VKD3D-Proton** como proceso auxiliar (D3D12 sobre RADV) | **EXPERIMENTAL — demostrado en aislamiento** | E1/E2: con GE-Proton11-6 el loader ofrece 4.1.1, crea contexto y despacha 16/16 frames en la RX 9060 XT. Con Proton Experimental 11.0-20261001 el proveedor 4.1.1 no aparece. Depende de un runtime Proton concreto. |
| C | Interop Minecraft (Vulkan nativo) ↔ proceso Wine auxiliar | **UNVERIFIED** | Wine no expone `VK_KHR_external_memory_fd` a apps Windows ni puede importar dma-buf de procesos nativos de forma documentada. Opciones: (C1) memoria compartida en el host con copias GPU→CPU→GPU (funcional pero con coste alto de ancho de banda y latencia), (C2) zero-copy vía dma-buf: no demostrado. Se prototipará (E3). |
| D | Minecraft Windows completo bajo Wine (Vulkan + D3D12 en el mismo proceso, recursos compartidos win32) | **UNVERIFIED** | Contradice el objetivo "nativo Linux"; solo como referencia. |
| E | Portar el modelo a ROCm/MIGraphX/Vulkan propio | **BLOCKED** | El modelo no se publica en formato abierto; extraerlo del DLL sería ingeniería inversa de pesos, fuera del alcance legítimo y no necesario. No se intenta. |
| F | OptiScaler/Proton "FSR4 upgrade" | **BLOCKED para este caso** | Actúa sobre juegos DX12/DX11 bajo Proton; Minecraft usa Vulkan. |
| G | FSR 3.1.5 (misma SDK) | ver más abajo | Las implementaciones **Vulkan** de FSR 3.1.x pertenecían al SDK 1.1.x (v1.1.4 incluye correcciones Vulkan). Es una ruta distinta de FSR 4; no se presenta como FSR 4. |

## Conclusión provisional
- "FSR 4.1.1 oficial nativo en Vulkan": **no existe** en el SDK público.
- "FSR 4.1.1 oficial en este PC": **ejecutable** vía DX12→VKD3D-Proton (E2), con GE-Proton11-6. Lo que queda por demostrar es el puente con Minecraft (C). El mod debe etiquetar este backend como *FSR 4.1.1 (puente Wine/VKD3D-Proton)* y no activarlo hasta que el puente funcione de extremo a extremo.
- No se puede declarar "FSR 4.1.1 REAL FUNCIONANDO" en Minecraft hasta cumplir los 7 criterios de la Fase 6 dentro del juego.

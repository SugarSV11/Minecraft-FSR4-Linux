# Auditoría del renderizador de Minecraft 26.3 (parcial)

Fuente: `client.jar` oficial 26.3 (sha1 `e877b6a0…7b87`, verificado), sin ofuscar. Inspección con `javap`. Todo lo listado existe en el jar; lo no verificado está marcado **PENDIENTE**.

## Verificado
- Versión final **26.3** (publicada 2026-09-15; `world_version` 5023, Java 25, LWJGL 3.4.3). El `version_manifest_v2.json` local del launcher estaba desfasado.
- Selección de API: opción `preferredGraphicsBackend` (`options.txt`, valores `default|opengl|vulkan`; enum `net.minecraft.client.PreferredGraphicsApi`). Con `"vulkan"` el log muestra `Using graphics backend Vulkan, using drivers: 1.4.354 radv Mesa 26.2.4` y `Using graphics device: AMD Radeon RX 9060 XT (RADV GFX1200)` (ejecución real, perfil aislado `fabric/run`).
- Capa RenderPearl (`com.mojang.renderpearl`): `api/` (GpuDevice, GpuTexture, GpuTextureView, CommandEncoder, RenderPass…), `backend/vulkan/` (39 clases) y `backend/opengl/`.
- **Dispositivo Vulkan real**: `VulkanDevice` expone `vkDevice()` (`org.lwjgl.vulkan.VkDevice`), `instance()`, `vma()` (allocator VMA), `graphicsQueue()/computeQueue()/transferQueue()` (`VulkanQueue{vkQueue, queueFamilyIndex}`) y `checkpointExtension()`. → Se puede reutilizar el dispositivo sin crear uno segundo.
- **Imágenes**: `VulkanGpuTexture.vkImage()` (long), `VulkanGpuTextureView.vkImageView()`; creación `VulkanDevice.createTexture(String,int usage,GpuFormat,int w,int h,int depthOrLayers,int mips)`.
- **Command buffers y sincronización**: `VulkanCommandEncoder` tiene `allocateAndBeginTransientCommandBuffer()`, `execute(VkCommandBuffer)`, `waitSemaphore(long,long,long)`, `signalSemaphore(long,long,long)`, `submit()`, `MAX_SUBMITS_IN_FLIGHT`, semáforo de timeline `submitSemaphore`, `DestructionQueue` (destrucción diferida de recursos). → hay ganchos nativos para inyectar trabajo de cómputo propio.
- **Presentación**: `VulkanGpuSurface` (swapchain propia; `acquireNextTexture`, `blitFromTexture(CommandEncoderBackend, GpuTextureView)`, `present`). La imagen final se blitea desde un `GpuTextureView`.
- **Capa de juego**: `GameRenderer` mantiene `mainRenderTarget` (`com.mojang.blaze3d.pipeline.RenderTarget`), `hud3DTarget`, `levelProjectionMatrixBuffer`/`hud3dProjectionMatrixBuffer` (`ProjectionMatrixBuffer`), `mainCamera`, `PostChain`s y `guiRenderer`; métodos `extract(DeltaTracker, boolean)`, `render()`, `renderLevel()`, `resize(int,int)`, `mainRenderTarget()`. Pipeline "extract → render" (`LevelExtractor`, `GameRenderState`).
- API de mods verificada (Fabric API 0.162.0+26.3): `KeyMappingHelper`, `KeyMapping.consumeClick()`, `HudElementRegistry.addLast(Identifier, HudElement)` con `HudElement.extractRenderState(GuiGraphicsExtractor, DeltaTracker)`, `GuiGraphicsExtractor.fill/text/centeredText/guiWidth`.
- Proyectos de referencia: `lukeclaw/dlssmc` (CC0; mixins `Vulkan{Instance,Backend,Device,GpuSurface,GpuSampler}Mixin`, `GameRendererMixin`, `LevelRendererMixin`; FFM en vez de JNI; jitter Halton(2,3), pasada propia de vectores de movimiento con profundidad, mundo a resolución interna y HUD nativo), `Gabrieli2806/Radiante-26.3-Fabric` (GPL-3.0 → **no se copia código**; comparte el dispositivo de Minecraft), `mc-fsr` (MIT en CurseForge, sin repositorio público localizado; movimiento solo por cámara).

## PENDIENTE (no se afirma nada todavía)
- Dónde exactamente se compone el HUD frente al render del mundo y cómo redimensionar `mainRenderTarget` a resolución interna.
- Matrices de cámara/proyección usables para jitter (`Projection`, `ProjectionMatrixBuffer`), exposición, layouts/barreras exactas de `mainRenderTarget`.
- Vectores de movimiento: Minecraft **no** los produce; hay que reconstruirlos (cámara + profundidad) o generarlos por geometría (limitación de calidad: entidades/follaje se animan sin vectores propios).
- Cambio de resolución / pantalla completa / recreación del renderizador en caliente.

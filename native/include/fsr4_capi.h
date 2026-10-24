/* C API of libfsr4linux.so, consumed from Java through the FFM API (no JNI). All handles are opaque; all functions are
 * called from the Minecraft render thread EXCEPT fsr4_bridge_start (blocks for the helper start-up: call it off-thread). */
#pragma once
#include <stdint.h>
#define FSR4_API __attribute__((visibility("default")))
#ifdef __cplusplus
extern "C" {
#endif
typedef struct Fsr4 Fsr4;
/* FFM checks this before creating a bridge or starting Wine. */
FSR4_API int fsr4_abi_info(uint64_t* values, int capacity);

FSR4_API Fsr4* fsr4_create(const char* protonDir, const char* prefixDir, const char* steamDir, const char* helperExe, const char* logFile,
                  int maxRenderW, int maxRenderH, int maxOutW, int maxOutH);
FSR4_API void fsr4_destroy(Fsr4*);
FSR4_API const char* fsr4_last_error(Fsr4*);

/* Bridge process (Wine helper running the official AMD FidelityFX DLLs). provider: "4.1.1" or "3.1.5". Returns 0 on success. */
FSR4_API int fsr4_bridge_start(Fsr4*, const char* provider);
FSR4_API int fsr4_bridge_running(Fsr4*);
FSR4_API void fsr4_bridge_stop(Fsr4*);
/* Provider reported by the AMD runtime after the first context was created ("" before). Returns length. */
FSR4_API int fsr4_active_provider(Fsr4*, char* buf, int len);
/* Providers the AMD runtime offers for this GPU, space separated (valid once the bridge is running). */
FSR4_API int fsr4_offered_providers(Fsr4*, char* buf, int len);

/* GPU side, on Minecraft's own Vulkan device. instance/phys/device/queue are raw Vulkan handles. Returns 0 on success. */
FSR4_API int fsr4_gpu_init(Fsr4*, uint64_t instance, uint64_t physicalDevice, uint64_t device, uint64_t queue, uint32_t queueFamily);
/* Creates + imports + verifies the shared planes for render/output sizes (RGBA8 color in/out) and binds Minecraft's depth image. */
FSR4_API int fsr4_gpu_alloc(Fsr4*, int renderW, int renderH, int outW, int outH, uint64_t mcDepthImage);
FSR4_API void fsr4_gpu_release(Fsr4*);

/* Records the input preparation (depth+motion vectors+color copy) into an already-begun command buffer. */
FSR4_API int fsr4_record_input(Fsr4*, uint64_t commandBuffer, uint64_t mcColorImage, const float* matrices48, const float* camDelta3);
/* Split input recording: world pass BEFORE Minecraft draws the first-person hand (it clears the depth buffer first), hand+colour pass after the world is complete. */
FSR4_API int fsr4_record_world(Fsr4*, uint64_t commandBuffer, const float* matrices48, const float* camDelta3);
FSR4_API int fsr4_record_hand_color(Fsr4*, uint64_t commandBuffer, uint64_t mcColorImage);
/* Per-entity rigid motion for the next world pass: n x 16 floats (pivot.xyz+dyaw, bboxCentreOffset.xyz, halfExtents.xyz, displacement.xyz), n<=48. */
FSR4_API int fsr4_set_entities(Fsr4*, const float* data, int n);
/* Quad particles for the next fsr4_record_world (8 floats each: camera-relative centre, half extent X in px, displacement since the previous frame, half extent Y in px). n=0: none. */
FSR4_API int fsr4_set_particles(Fsr4*, const float* data, int n);
/* FG x3 (EXPERIMENTAL own backend, no AMD code). alloc needs fsr4_gpu_alloc first (uses its depth + motion-vector planes). record_generate returns 1 when a frame was generated, 0 when there is no previous frame yet. */
/* Process-wide: use the 64-bit-atomic single-pass FG x3/x4 shader (call BEFORE fsr4_gpu_init; the VkDevice needs shaderBufferInt64Atomics + shaderInt64). */
FSR4_API void fsr4_x3_set_int64(int on);
FSR4_API int fsr4_x3_alloc(Fsr4*, int dispW, int dispH);
FSR4_API void fsr4_x3_release(Fsr4*);
FSR4_API int fsr4_x3_record_hudless(Fsr4*, uint64_t commandBuffer, uint64_t mcColorImage);
FSR4_API int fsr4_x3_record_generate(Fsr4*, uint64_t commandBuffer, uint64_t mcFinalImage, int idx, uint64_t dstImage);
FSR4_API int fsr4_x3_record_generate_n(Fsr4*, uint64_t commandBuffer, uint64_t mcFinalImage, int idx, int n, uint64_t dstImage);   /* t = idx/n (n = 3 or 4) */
FSR4_API int fsr4_x3_record_end(Fsr4*, uint64_t commandBuffer);
FSR4_API void fsr4_x3_invalidate(Fsr4*);
FSR4_API int fsr4_x3_gpu_ms(Fsr4*, float* ms);   /* FSR4_GPU_TS=1; 0 = valid */
FSR4_API uint64_t fsr4_x3_bytes(Fsr4*);
FSR4_API int fsr4_gpu_times(Fsr4*, float* out4);   /* FSR4_GPU_TS=1: prep, upscale chain, copy-out, other (ms); 0 = valid */
FSR4_API int fsr4_record_output(Fsr4*, uint64_t commandBuffer, uint64_t dstColorImage);
FSR4_API uint64_t fsr4_timeline_semaphore(Fsr4*);
FSR4_API int fsr4_wait_timeline(Fsr4*, uint64_t value, uint64_t timeoutNs);

/* One upscale request (provider "4.1.1"/"3.1.5": a change recreates the FFX context). ints = {frameId, renderW, renderH, outW, outH, flags, quality, enableSharpening}
 * floats = {jitterX, jitterY, mvScaleX, mvScaleY, frameTimeMs, preExposure, sharpness, near, far, fovY}
 * stats  = {gpuDispatchMs, helperTotalMs, status, ffxReturnCode}. Returns 0 on success. */
FSR4_API int fsr4_process(Fsr4*, const int32_t* ints, const float* floats, const char* provider, float* stats);

/* Shared-fence (GPU-to-GPU) path. fsr4_sync_init imports the helper's D3D12 shared fence as a Vulkan timeline semaphore on the caller's device
 * (needs VK_KHR_external_semaphore_fd) and self-tests it; returns -1 + last_error when unsupported (caller stays on the CPU-wait path).
 * Async frame: record_input -> submit signalling `wait`; fsr4_process_async returns once the helper has SUBMITTED its work; the caller's
 * GPU must wait on `signal` (fsr4_shared_semaphore) before reading the output. fsr4_signal_shared unblocks that wait if the helper failed. */
FSR4_API int fsr4_sync_init(Fsr4*);
FSR4_API uint64_t fsr4_shared_semaphore(Fsr4*);
FSR4_API int fsr4_sync_values(Fsr4*, uint64_t* wait, uint64_t* signal);
FSR4_API int fsr4_process_async(Fsr4*, const int32_t* ints, const float* floats, const char* provider, float* stats, uint64_t wait, uint64_t signal);
FSR4_API int64_t fsr4_shared_counter(Fsr4*);   /* current value of the shared timeline (-1 if none): a GPU wait on a higher value is still pending */
FSR4_API int fsr4_signal_shared(Fsr4*, uint64_t value);

/* Disable GPU-to-GPU synchronization (shared fence) and force CPU-wait path.
 * Use when running in environments where fd passing is blocked (e.g., Flatpak sandbox). */
FSR4_API void fsr4_disable_shared_fence(Fsr4*);

/* Frame generation (FSR FG API 4.0.1). fsr4_fg_alloc needs fsr4_gpu_alloc first (depth + motion vector planes come from it); it creates the helper's FG context and two
 * shared display-size RGBA8 planes. Per real frame: fsr4_record_fg_input (final colour incl. HUD -> helper), fsr4_process_fg, fsr4_record_fg_output (generated frame <- helper). */
FSR4_API int fsr4_fg_alloc(Fsr4*, int displayW, int displayH, const char* provider, int ctxFlags);
FSR4_API void fsr4_fg_release(Fsr4*);
FSR4_API int fsr4_fg_ready(Fsr4*);
FSR4_API int fsr4_fg_provider(Fsr4*, char* buf, int len);   /* provider name reported by the AMD runtime for the FG context */
FSR4_API int fsr4_record_fg_input(Fsr4*, uint64_t commandBuffer, uint64_t finalColorImage);
FSR4_API int fsr4_record_fg_hudless(Fsr4*, uint64_t commandBuffer, uint64_t worldColorImage);   /* world colour BEFORE the HUD: FG extracts the UI and composes it over the generated frame */
FSR4_API int fsr4_fg_gpu_times(Fsr4*, float* out4);   /* FSR4_GPU_TS=1: input copy, FG chain, output copy, gap (ms); 0 = valid */
FSR4_API int fsr4_record_fg_output(Fsr4*, uint64_t commandBuffer, uint64_t dstImage);
FSR4_API int fsr4_fg_post(Fsr4*, const int32_t* ints, const float* floats, uint64_t wait, uint64_t signal);   /* asynchronous: collect before any other bridge call */
FSR4_API int fsr4_fg_collect(Fsr4*, float* stats);
FSR4_API int fsr4_fg_outstanding(Fsr4*);
FSR4_API int fsr4_process_fg(Fsr4*, const int32_t* ints, const float* floats, float* stats, uint64_t wait, uint64_t signal);
FSR4_API int fsr4_debug_dump(Fsr4*, int plane, const char* path, char* report, int len);   /* debug: statistics of shared plane 1 (depth) / 2 (motion vectors) */
#ifdef __cplusplus
}
#endif

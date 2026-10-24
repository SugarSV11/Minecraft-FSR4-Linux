#include "fsr4_capi.h"
#include <dlfcn.h>
#include <cstring>
#include <memory>
#include <string>
#include "prep_pass.hpp"
#include "fgx3_pass.hpp"

using namespace fsr4;
using namespace fsr4ipc;

struct Fsr4 {
    BridgeConfig cfg; BridgeClient bc; GpuBridge gb; PrepPass pp; FgX3Pass x3; VkCtx ctx{}; std::string err; bool gpuInit = false, allocated = false;
    int ow = 0, oh = 0;
};

static int fail(Fsr4* h, const std::string& m) { h->err = m; return -1; }

extern "C" {
int fsr4_abi_info(uint64_t* values, int capacity) {
    if (!values || capacity < 8) return -1;
    const uint64_t abi[] = {1, kVersion, sizeof(Header), alignof(Header), sizeof(Request),
                            alignof(Request), sizeof(Response), sizeof(Layout)};
    memcpy(values, abi, sizeof abi);
    return 8;
}
Fsr4* fsr4_create(const char* proton, const char* prefix, const char* steam, const char* exe, const char* log, int mrw, int mrh, int mow, int moh) {
    auto* h = new Fsr4(); h->cfg.protonDir = proton; h->cfg.prefixDir = prefix; h->cfg.steamDir = steam; h->cfg.helperExe = exe; h->cfg.logFile = log;
    h->cfg.maxRenderW = (uint32_t)mrw; h->cfg.maxRenderH = (uint32_t)mrh; h->cfg.maxOutW = (uint32_t)mow; h->cfg.maxOutH = (uint32_t)moh; h->cfg.transferMode = 2; return h;
}
void fsr4_destroy(Fsr4* h) { if (!h) return; fsr4_gpu_release(h); h->gb.destroyShared(); h->bc.stop(); delete h; }
const char* fsr4_last_error(Fsr4* h) { return h->err.c_str(); }

int fsr4_bridge_start(Fsr4* h, const char* provider) {
    h->gb.destroyShared();   // a semaphore imported from a previous helper must never be reused (its fence is gone: GPU waits on it would never finish)
    h->cfg.provider = provider; if (!h->bc.start(h->cfg)) return fail(h, h->bc.error()); return 0;
}
int fsr4_bridge_running(Fsr4* h) { return h->bc.running() ? 1 : 0; }
void fsr4_bridge_stop(Fsr4* h) { fsr4_gpu_release(h); h->gb.destroyShared(); h->bc.stop(); }
int fsr4_active_provider(Fsr4* h, char* buf, int len) { std::string p = h->bc.activeProvider(); snprintf(buf, (size_t)len, "%s", p.c_str()); return (int)p.size(); }

int fsr4_gpu_init(Fsr4* h, uint64_t inst, uint64_t phys, uint64_t dev, uint64_t queue, uint32_t family) {
    static void* lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_NOLOAD); if (!lib) lib = dlopen("libvulkan.so.1", RTLD_NOW);
    if (!lib) return fail(h, "libvulkan.so.1 not loadable");
    h->ctx.gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr"); if (!h->ctx.gipa) return fail(h, "vkGetInstanceProcAddr missing");
    h->ctx.instance = (VkInstance)inst; h->ctx.phys = (VkPhysicalDevice)phys; h->ctx.device = (VkDevice)dev; h->ctx.queue = (VkQueue)queue; h->ctx.queueFamily = family;
    std::string e; if (!h->gb.init(h->ctx, &h->bc, &e)) return fail(h, e); if (!h->pp.init(h->ctx, &e)) return fail(h, e); if (!h->x3.init(h->ctx, &e)) return fail(h, e); h->gpuInit = true; return 0;
}
int fsr4_gpu_alloc(Fsr4* h, int rw, int rh, int ow, int oh, uint64_t depthImage) {
    if (!h->gpuInit) return fail(h, "gpu not initialised"); std::string e; h->allocated = false;
    h->x3.release();   // holds views of the planes that are about to be replaced
    h->pp.unbind();
    if (!h->gb.allocate((uint32_t)rw, (uint32_t)rh, (uint32_t)ow, (uint32_t)oh, Fmt::RGBA8, Fmt::RGBA8, &e)) return fail(h, e);
    if (!h->pp.bind(&h->gb, (VkImage)depthImage, &e)) return fail(h, e);
    h->ow = ow; h->oh = oh; h->allocated = true; return 0;
}
void fsr4_gpu_release(Fsr4* h) { if (!h->gpuInit) return; h->x3.destroy(); h->allocated = false; h->pp.unbind(); h->gb.release(); h->pp.destroy(); h->gpuInit = false; }
int fsr4_record_input(Fsr4* h, uint64_t cb, uint64_t color, const float* m, const float* d) { if (!h->allocated) return fail(h, "not allocated"); h->pp.record((VkCommandBuffer)cb, (VkImage)color, m, d); return 0; }
int fsr4_record_world(Fsr4* h, uint64_t cb, const float* m, const float* d) { if (!h->allocated) return fail(h, "not allocated"); h->pp.recordWorld((VkCommandBuffer)cb, m, d); return 0; }
int fsr4_set_entities(Fsr4* h, const float* d, int n) { h->pp.setEntities(d, n); return 0; }
int fsr4_set_particles(Fsr4* h, const float* d, int n) { h->pp.setParticles(d, n); return 0; }
void fsr4_x3_set_int64(int on) { FgX3Pass::setInt64(on != 0); }
int fsr4_x3_alloc(Fsr4* h, int dw, int dh) { if (!h->allocated) return fail(h, "not allocated"); std::string e; if (!h->x3.alloc((uint32_t)dw, (uint32_t)dh, h->gb.renderW(), h->gb.renderH(), h->gb.image(1), h->gb.image(2), &e)) return fail(h, e); return 0; }
void fsr4_x3_release(Fsr4* h) { h->x3.release(); }
int fsr4_x3_record_hudless(Fsr4* h, uint64_t cb, uint64_t color) { h->x3.recordHudless((VkCommandBuffer)cb, (VkImage)color); return 0; }
int fsr4_x3_record_generate(Fsr4* h, uint64_t cb, uint64_t finalColor, int idx, uint64_t dst) { return h->x3.recordGenerate((VkCommandBuffer)cb, (VkImage)finalColor, idx, (VkImage)dst) ? 1 : 0; }
int fsr4_x3_record_generate_n(Fsr4* h, uint64_t cb, uint64_t finalColor, int idx, int n, uint64_t dst) { return h->x3.recordGenerateN((VkCommandBuffer)cb, (VkImage)finalColor, idx, n, (VkImage)dst) ? 1 : 0; }
int fsr4_x3_record_end(Fsr4* h, uint64_t cb) { h->x3.recordEndFrame((VkCommandBuffer)cb); return 0; }
void fsr4_x3_invalidate(Fsr4* h) { h->x3.invalidate(); }
int fsr4_x3_gpu_ms(Fsr4* h, float* ms) { return h->x3.gpuMs(ms) ? 0 : -1; }
uint64_t fsr4_x3_bytes(Fsr4* h) { return h->x3.deviceBytes(); }
int fsr4_record_hand_color(Fsr4* h, uint64_t cb, uint64_t color) { if (!h->allocated) return fail(h, "not allocated"); h->pp.recordHandAndColor((VkCommandBuffer)cb, (VkImage)color); return 0; }
int fsr4_record_output(Fsr4* h, uint64_t cb, uint64_t dst) { if (!h->allocated) return fail(h, "not allocated"); h->pp.recordCopyOut((VkCommandBuffer)cb, (VkImage)dst); return 0; }
int fsr4_gpu_times(Fsr4* h, float* out4) { return h->allocated && h->pp.gpuTimes(out4) ? 0 : 1; }
uint64_t fsr4_timeline_semaphore(Fsr4* h) { return (uint64_t)h->pp.timeline(); }
int fsr4_wait_timeline(Fsr4* h, uint64_t v, uint64_t ns) { return h->pp.waitTimeline(v, ns) ? 0 : fail(h, "timeline wait failed/timeout"); }

int fsr4_offered_providers(Fsr4* h, char* buf, int len) { std::string p = h->bc.offeredProviders(); snprintf(buf, (size_t)len, "%s", p.c_str()); return (int)p.size(); }

static int processImpl(Fsr4* h, const int32_t* i, const float* f, const char* provider, float* stats, uint64_t fenceWait, uint64_t fenceSignal) {
    if (!h->allocated) return fail(h, "not allocated");
    FrameParams p; p.fenceWait = fenceWait; p.fenceSignal = fenceSignal; p.provider = provider ? provider : ""; p.cmd = kCmdFrame; p.frameId = (uint64_t)i[0]; p.renderW = (uint32_t)i[1]; p.renderH = (uint32_t)i[2]; p.outW = (uint32_t)i[3]; p.outH = (uint32_t)i[4]; p.flags = (uint32_t)i[5]; p.quality = (uint32_t)i[6]; p.enableSharpening = i[7] != 0;
    p.colorFmt = (uint32_t)Fmt::RGBA8; p.outFmt = (uint32_t)Fmt::RGBA8; p.jitterX = f[0]; p.jitterY = f[1]; p.mvScaleX = f[2]; p.mvScaleY = f[3]; p.frameTimeMs = f[4]; p.preExposure = f[5]; p.sharpness = f[6]; p.cameraNear = f[7]; p.cameraFar = f[8]; p.fovY = f[9];
    Response r{}; BridgeResult br = h->bc.submit(p, 2000, &r);
    stats[0] = r.gpuDispatchMs; stats[1] = r.helperTotalMs; stats[2] = (float)r.status; stats[3] = (float)r.ffxReturnCode; stats[4] = r.helperUploadMs; stats[5] = r.helperReadbackMs;
    if (br == BridgeResult::Ok) return 0;
    static const char* names[] = {"Ok", "helper not running", "helper timeout", "helper died", "helper stalled", "request rejected", "internal"};
    return fail(h, std::string(names[(int)br]) + (br == BridgeResult::Rejected ? std::string(": ") + r.message : ""));
}
int fsr4_process(Fsr4* h, const int32_t* i, const float* f, const char* provider, float* stats) { return processImpl(h, i, f, provider, stats, 0, 0); }
int fsr4_process_async(Fsr4* h, const int32_t* i, const float* f, const char* provider, float* stats, uint64_t w, uint64_t sgl) { return processImpl(h, i, f, provider, stats, w, sgl); }
int fsr4_sync_init(Fsr4* h) { if (!h->gpuInit) return fail(h, "gpu not initialised"); std::string e; return h->gb.initSharedFence(&e) ? 0 : fail(h, e); }
uint64_t fsr4_shared_semaphore(Fsr4* h) { return (uint64_t)h->gb.sharedSemaphore(); }
int fsr4_sync_values(Fsr4* h, uint64_t* w, uint64_t* s) { return h->gb.nextFenceValues(w, s) ? 0 : fail(h, "no shared fence"); }
int64_t fsr4_shared_counter(Fsr4* h) { return h->gb.sharedCounter(); }
int fsr4_signal_shared(Fsr4* h, uint64_t v) { return h->gb.signalShared(v) ? 0 : -1; }
int fsr4_fg_alloc(Fsr4* h, int dispW, int dispH, const char* provider, int flags) {
    if (!h->allocated) return fail(h, "not allocated"); std::string e;
    if (!h->gb.allocateFg((uint32_t)dispW, (uint32_t)dispH, provider, (uint32_t)flags, &e)) return fail(h, e); return 0;
}
void fsr4_fg_release(Fsr4* h) { if (h->gpuInit) h->gb.releaseFg(true); }
int fsr4_fg_provider(Fsr4* h, char* buf, int len) { snprintf(buf, (size_t)len, "%s", h->gb.fgProvider().c_str()); return (int)h->gb.fgProvider().size(); }
int fsr4_fg_ready(Fsr4* h) { return h->gb.fgReady() ? 1 : 0; }
int fsr4_record_fg_input(Fsr4* h, uint64_t cb, uint64_t finalColorImage) { if (!h->gb.fgReady()) return fail(h, "frame generation not ready"); h->pp.fgMark((VkCommandBuffer)cb, 0); h->gb.recordFgIn((VkCommandBuffer)cb, (VkImage)finalColorImage); h->pp.fgMark((VkCommandBuffer)cb, 1); return 0; }
int fsr4_record_fg_hudless(Fsr4* h, uint64_t cb, uint64_t worldColorImage) { if (!h->gb.fgReady()) return fail(h, "frame generation not ready"); h->gb.recordFgHudless((VkCommandBuffer)cb, (VkImage)worldColorImage); return 0; }
int fsr4_record_fg_output(Fsr4* h, uint64_t cb, uint64_t dstImage) { if (!h->gb.fgReady()) return fail(h, "frame generation not ready"); h->pp.fgMark((VkCommandBuffer)cb, 2); h->gb.recordFgOut((VkCommandBuffer)cb, (VkImage)dstImage); h->pp.fgMark((VkCommandBuffer)cb, 3); return 0; }
int fsr4_fg_gpu_times(Fsr4* h, float* out4) { return h->pp.fgGpuTimes(out4) ? 0 : 1; }
/* ints = {frameId, flags}; floats = {jitterX, jitterY, mvScaleX, mvScaleY, frameTimeMs, near, far, fovY, cam[12]}; stats = {gpuFgMs(previous frame), helperTotalMs, status, ffxReturnCode, cpuRecordMs} */
static BridgeResult fgPost(Fsr4* h, const int32_t* i, const float* f, uint64_t wait, uint64_t signal) {
    FrameParams p; p.cmd = kCmdFgFrame; p.frameId = (uint64_t)i[0]; p.flags = (uint32_t)i[1]; p.jitterX = f[0]; p.jitterY = f[1]; p.mvScaleX = f[2]; p.mvScaleY = f[3]; p.frameTimeMs = f[4]; p.cameraNear = f[5]; p.cameraFar = f[6]; p.fovY = f[7];
    memcpy(p.fgCam, f + 8, 12 * sizeof(float)); p.fenceWait = wait; p.fenceSignal = signal; p.renderW = 0; return h->bc.post(p);
}
static int fgCollectImpl(Fsr4* h, float* stats) {
    Response r{}; BridgeResult br = h->bc.collect(2000, &r);
    stats[0] = r.gpuDispatchMs; stats[1] = r.helperTotalMs; stats[2] = (float)r.status; stats[3] = (float)r.ffxReturnCode; stats[4] = r.helperUploadMs;
    if (br == BridgeResult::Ok) return 0;
    static const char* names[] = {"Ok", "helper not running", "helper timeout", "helper died", "helper stalled", "request rejected", "internal"};
    return fail(h, std::string(names[(int)br]) + (br == BridgeResult::Rejected ? std::string(": ") + r.message : ""));
}
int fsr4_process_fg(Fsr4* h, const int32_t* i, const float* f, float* stats, uint64_t wait, uint64_t signal) {
    if (!h->gb.fgReady()) return fail(h, "frame generation not ready");
    BridgeResult pr = fgPost(h, i, f, wait, signal); if (pr != BridgeResult::Ok) return fail(h, "fg post failed"); return fgCollectImpl(h, stats);
}
/* Asynchronous form: post returns once the request is published (the helper records + submits in parallel with the caller); collect (before ANY other bridge call) returns its status. */
int fsr4_fg_post(Fsr4* h, const int32_t* i, const float* f, uint64_t wait, uint64_t signal) {
    if (!h->gb.fgReady()) return fail(h, "frame generation not ready");
    BridgeResult pr = fgPost(h, i, f, wait, signal); return pr == BridgeResult::Ok ? 0 : fail(h, pr == BridgeResult::Internal ? "previous FG request not collected" : "fg post failed");
}
int fsr4_fg_collect(Fsr4* h, float* stats) { return fgCollectImpl(h, stats); }
int fsr4_fg_outstanding(Fsr4* h) { return h->bc.outstanding() ? 1 : 0; }
int fsr4_debug_dump(Fsr4* h, int plane, const char* path, char* report, int len) { std::string r; bool ok = h->gb.debugDumpPlane(plane, path, &r); snprintf(report, (size_t)len, "%s", r.c_str()); return ok ? 0 : -1; }

/* Disable GPU-to-GPU synchronization (shared fence) and force CPU-wait path.
 * Use when running in environments where fd passing is blocked (e.g., Flatpak sandbox). */
FSR4_API void fsr4_disable_shared_fence(Fsr4* h) {
    if (!h) return;
    h->gb.destroyShared();
    h->gb.setSharedFenceDisabled(true);
}
}

// GPU side of the FSR bridge (transferMode 2): imports the dma-bufs of the D3D12 shared textures created by the
// Wine helper into the CALLER'S Vulkan device and verifies them with a CRC self-test before any frame is trusted.
// The caller (Minecraft via JNI, or tests/gpu_ipc_test.cpp) supplies its own VkInstance/VkDevice/queue.
#pragma once
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
#include "bridge_client.hpp"

namespace fsr4 {

struct VkCtx {
    VkInstance instance = nullptr; VkPhysicalDevice phys = nullptr; VkDevice device = nullptr;
    VkQueue queue = nullptr; uint32_t queueFamily = 0;
    PFN_vkGetInstanceProcAddr gipa = nullptr;   // from libvulkan; all other entry points are resolved from it
};

class GpuBridge {
public:
    ~GpuBridge() { release(); }
    bool init(const VkCtx& ctx, BridgeClient* client, std::string* err);
    // Creates the shared textures in the helper, imports them, verifies them (CRC round trip). On any failure returns
    // false with a concrete cause and leaves nothing imported.
    bool allocate(uint32_t renderW, uint32_t renderH, uint32_t outW, uint32_t outH, fsr4ipc::Fmt colorFmt, fsr4ipc::Fmt outFmt, std::string* err);
    // Shared timeline fence (D3D12 shared fence in the helper == native Vulkan timeline semaphore): GPU-to-GPU ordering without CPU waits.
    // Needs VK_KHR_external_semaphore(_fd) on the caller's device. Returns false + cause when unsupported (caller keeps the CPU-wait path).
    bool initSharedFence(std::string* err);
    VkSemaphore sharedSemaphore() const { return sem_; }
    bool nextFenceValues(uint64_t* wait, uint64_t* signal) { if (!sem_) return false; *wait = ++fenceValue_; *signal = ++fenceValue_; return true; }
    bool signalShared(uint64_t value);
    int64_t sharedCounter();
    void destroyShared();   // call when the bridge stops: the fence and its import live as long as the helper (re-imports would be ambiguous: closed fd numbers get reused)   // CPU-side signal (failure recovery: unblocks a GPU queue that waits for the helper)
    // Frame generation planes (FSR FG 4.0.1 in the helper): final colour in (display size, RGBA8) and generated frame out. Needs allocate() first.
    bool allocateFg(uint32_t dispW, uint32_t dispH, const char* provider, uint32_t flags, std::string* err);
    void releaseFg(bool tellHelper);
    bool fgReady() const { return fgReady_; }
    const std::string& fgProvider() const { return fgProvider_; }   // as reported by the AMD runtime when the FG context was created
    void recordFgIn(VkCommandBuffer cb, VkImage src);      // src (GENERAL) -> shared plane 4, with full barriers around
    void recordFgHudless(VkCommandBuffer cb, VkImage src); // world-only colour (before the HUD is drawn) -> shared plane 6
    void recordFgOut(VkCommandBuffer cb, VkImage dst);     // shared plane 5 -> dst (GENERAL), with full barriers around
    // Debug: copies shared plane `plane` (1 depth R32F, 2 motion vectors RG16F) to host memory after a full GPU idle, prints statistics and writes raw bytes to `path` (empty = no file).
    bool debugDumpPlane(int plane, const char* path, std::string* report);
    void release();   // caller must have waited for the GPU to be idle
    bool ready() const { return ready_; }
    VkImage image(int i) const { return img_[i]; }          // 0 color, 1 depth(R32F), 2 motion vectors(RG16F), 3 output, 4 final colour (FG input), 5 generated frame (FG output)
    VkFormat format(int i) const { return fmt_[i]; }
    const char* usageUsed() const { return usageName_; }
    // Disable shared fence (GPU-to-GPU sync) and force CPU-wait path.
    void setSharedFenceDisabled(bool disabled);
    // Records "copy local image -> shared image" for a plane of identical format and size. Both images must be in the
    // stated layouts; the shared image is kept in GENERAL layout at all times.
    void recordCopyToShared(VkCommandBuffer cb, int plane, VkImage src, VkImageLayout srcLayout);
    void recordCopyFromShared(VkCommandBuffer cb, int plane, VkImage dst, VkImageLayout dstLayout);
    uint32_t renderW() const { return rw_; } uint32_t renderH() const { return rh_; } uint32_t outW() const { return ow_; } uint32_t outH() const { return oh_; }
    // Fixed-function table (resolved once); public so tests can reuse it.
    struct Fns;
private:
    bool importAll(uint32_t usageIdx, int first, int last, std::string* err);
    void globalBarrier(VkCommandBuffer cb);
    bool selfTest(std::string* err);
    void destroyImages();
    VkCtx ctx_{}; BridgeClient* cl_ = nullptr; Fns* f_ = nullptr;
    static constexpr int kPlanes = 7;
    VkImage img_[kPlanes] = {}; VkDeviceMemory mem_[kPlanes] = {}; VkFormat fmt_[kPlanes] = {}; uint32_t w_[kPlanes] = {}, h_[kPlanes] = {};
    int fds_[kPlanes] = {-1, -1, -1, -1, -1, -1, -1}; int nPlanes_ = 4; bool fgReady_ = false; std::string fgProvider_; uint32_t usageIdx_ = 0;
    uint32_t rw_ = 0, rh_ = 0, ow_ = 0, oh_ = 0; fsr4ipc::Fmt cf_{}, of_{};
    VkSemaphore sem_ = nullptr; uint64_t fenceValue_ = 0; VkDevice semDev_ = nullptr; PFN_vkDestroySemaphore destroySem_ = nullptr; PFN_vkGetSemaphoreCounterValue getCounter_ = nullptr;
    VkCommandPool pool_ = nullptr; bool ready_ = false; bool allocatedInHelper_ = false; const char* usageName_ = "";
    uint32_t memProps_[VK_MAX_MEMORY_TYPES] = {};
    bool sharedFenceDisabled_ = false;
};

}  // namespace fsr4

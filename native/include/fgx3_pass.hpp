// FG x3 (EXPERIMENTAL, own backend, no AMD code): generates two intermediate frames between the previous and the current real frame with a compute pass inside Minecraft's
// Vulkan device (shaders/fgx3.comp; the isolated prototype and its measurements live in tools/fgx3). Inputs are Minecraft's planes: depth + motion vectors (the prep pass
// outputs, GENERAL layout) and the colour images this class copies from Minecraft (world-only colour and final colour). All images are device-local and private to this class.
#pragma once
#include <string>
#include "gpu_bridge.hpp"

namespace fsr4 {

class FgX3Pass {
public:
    ~FgX3Pass() { destroy(); }
    // Process-wide switch, set BEFORE init(): use the single-pass 64-bit-atomic variant of the shader (the VkDevice must have shaderBufferInt64Atomics + shaderInt64 enabled). Default off.
    static void setInt64(bool on);
    bool int64() const { return i64_; }
    bool init(const VkCtx& ctx, std::string* err);
    // depth: R32_SFLOAT render-res plane, mv: R16G16_SFLOAT render-res plane (both GENERAL, owned by the caller).
    bool alloc(uint32_t dispW, uint32_t dispH, uint32_t rendW, uint32_t rendH, VkImage depth, VkImage mv, std::string* err);
    void release();
    void destroy();  // release per-size resources and the Vulkan objects created by init(), while the VkDevice is still alive
    // World-only colour (before the HUD) of the CURRENT frame -> private image. Recorded once per frame, after the world is finished.
    void recordHudless(VkCommandBuffer cb, VkImage mcColor);
    // Generates intermediate frame idx (1 = t=1/3, 2 = t=2/3) and copies it into dst (GENERAL). idx==1 also copies mcFinal (colour incl. HUD) first. Returns false when no previous frame exists yet.
    bool recordGenerate(VkCommandBuffer cb, VkImage mcFinal, int idx, VkImage dst) { return recordGenerateN(cb, mcFinal, idx, 3, dst); }
    // Generic multiplier: frame idx of n (t = idx/n, idx = 1..n-1; n=3 -> x3, n=4 -> x4 EXPERIMENTAL). idx==1 copies mcFinal first; idx==n-1 closes the GPU-time measurement.
    bool recordGenerateN(VkCommandBuffer cb, VkImage mcFinal, int idx, int n, VkImage dst);
    // Same with an explicit time t in (0,1) (offline evaluation with measured frame timestamps); copyFinal = also copy mcFinal first.
    bool recordGenerateAt(VkCommandBuffer cb, VkImage mcFinal, float t, bool copyFinal, VkImage dst, bool tsEnd = false);
    bool readProfile(float* ms4);   // per-mode GPU ms of the first generated frame (clear, splat depth, record id, resolve+UI); FSR4_X3_PROFILE=1
    bool readTsNow(float* ms);   // offline profiling: GPU time between the copyFinal call and the tsEnd call just submitted (FSR4_GPU_TS=1; queue must be idle)
    // End of the real frame: the current frame becomes the previous one (depth copy + colour roles swap).
    void recordEndFrame(VkCommandBuffer cb);
    void invalidate() { havePrev_ = false; }       // camera cut / resize / reset
    bool ready() const { return havePrev_; }
    bool gpuMs(float* ms) const { if (!tsOn_ || !tsValid_) return false; *ms = tsMs_; return true; }   // FSR4_GPU_TS=1: both generations of the last completed frame
    uint64_t deviceBytes() const { return bytes_; }
    struct Fns;
private:
    VkCtx ctx_{}; Fns* f_ = nullptr; uint32_t dw_ = 0, dh_ = 0, rw_ = 0, rh_ = 0; bool havePrev_ = false, flipY_ = false, dbg_ = false, i64_ = false; int dbgMode_ = 0; int variant_ = 0; int cur_ = 0; uint64_t bytes_ = 0;
    VkDescriptorSetLayout dsl_ = nullptr; VkPipelineLayout pl_ = nullptr; VkPipeline pipe_ = nullptr; VkShaderModule sm_ = nullptr; VkDescriptorPool dp_ = nullptr; VkDescriptorSet ds_[2] = {};
    VkSampler lin_ = nullptr, nea_ = nullptr; VkCommandPool pool_ = nullptr;
    VkImage img_[6] = {}; VkDeviceMemory mem_[6] = {}; VkImageView view_[6] = {};   // 0,1 world colour (cur/prev roles swap), 2 final, 3 depthPrev, 4 out
    VkImage depthIn_ = nullptr; VkBuffer buf_[2] = {}; VkDeviceMemory bmem_[2] = {}; VkImageView depthInView_ = nullptr, mvView_ = nullptr;
    VkQueryPool qp_ = nullptr; bool tsOn_ = false, tsValid_ = false; double tsPeriod_ = 1.0; float tsMs_ = 0; uint32_t tsSlot_ = 0; uint64_t tsFrame_ = 0, tsSlotFrame_[8] = {};
    void pollTs();
    bool recordGenerateImpl(VkCommandBuffer cb, VkImage mcFinal, int idx, float tt, VkImage dst, bool tsEnd = false, int n = 3); uint32_t skip_ = 0; VkQueryPool qpP_ = nullptr; bool prof_ = false;
};

}  // namespace fsr4

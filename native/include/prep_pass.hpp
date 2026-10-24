// Compute pass that runs inside Minecraft's Vulkan queue and prepares the upscaler inputs:
// depth -> shared R32F plane, camera-reprojection motion vectors -> shared RG16F plane, color copy -> shared color plane.
#pragma once
#include <string>
#include <vector>
#include "gpu_bridge.hpp"

namespace fsr4 {

class PrepPass {
public:
    ~PrepPass() { destroy(); }
    bool init(const VkCtx& ctx, std::string* err);
    // (Re)binds to Minecraft's depth image (D32_SFLOAT, GENERAL layout) and the bridge's shared planes.
    bool bind(GpuBridge* gb, VkImage mcDepth, std::string* err);
    void unbind();
    // matrices: 3 column-major mat4 = invVpJit, prevVp, curVp; camDelta = pos(cur)-pos(prev).
    // The command buffer must already be in the recording state (as returned by Minecraft's encoder).
    void record(VkCommandBuffer cb, VkImage mcColor, const float* matrices48, const float* camDelta3);
    // Split form used by the game integration (Minecraft clears the depth buffer before drawing the first-person hand, so the world depth must be read BEFORE that):
    //  recordWorld: depth + motion vectors (call before the hand is drawn); recordHandAndColor: hand pixels get zero motion + their depth, and the final world colour is copied.
    void recordWorld(VkCommandBuffer cb, const float* matrices48, const float* camDelta3);
    // Per-entity rigid motion (16 floats each: pivot+dyaw, bbox centre offset, bbox half extents, displacement), used by the next recordWorld. n=0 clears.
    static constexpr int kMaxEnt = 48;
    void setEntities(const float* data, int n);
    // Quad particles for the next recordWorld (8 floats each: centre rel, half extent X px, displacement, half extent Y px). n=0 disables the particle sub-pass. Up to kMaxParticles.
    static constexpr int kMaxParticles = 4096;
    void setParticles(const float* data, int n);
    void recordHandAndColor(VkCommandBuffer cb, VkImage mcColor);
    void recordCopyOut(VkCommandBuffer cb, VkImage mcDstColor);
    void destroy();
    VkSemaphore timeline() const { return sem_; }
    bool waitTimeline(uint64_t value, uint64_t timeoutNs);
    // GPU timestamps (enabled with FSR4_GPU_TS=1): ms of the most recent completed frame: [0] T0->T1 prep (depth/MV/colour copy), [1] T1->T2 upscale chain (helper IPC + D3D12 dispatch + handoff),
    // [2] T2->T3 copy-out, [3] T3(previous frame)->T0 everything else on the queue (HUD, Minecraft work, idle). Returns false when unavailable.
    bool gpuTimes(float* out4) const;
    // Frame-generation chain timestamps (same FSR4_GPU_TS switch). fgMark(cb, k): k=0 before the input copy, 1 after it, 2 after the GPU waited for the helper (output copy start), 3 end of the output copy.
    // fgGpuTimes: [0] input copy (T0->T1, includes everything the queue still had to finish), [1] FG chain T1->T2 (helper wake + FG dispatch + handoff), [2] output copy, [3] gap (previous T3 -> T0).
    void fgMark(VkCommandBuffer cb, int k);
    bool fgGpuTimes(float* out4) const;
    struct Fns;
private:
    void ts(VkCommandBuffer cb, uint32_t idx);
    void pollTs();
    VkQueryPool qp_ = nullptr; bool tsOn_ = false; double tsPeriod_ = 1.0; float tsOut_[4] = {0, 0, 0, 0}; bool tsValid_ = false; uint64_t tsFrame_ = 0; uint64_t tsSlotFrame_[64] = {};
    uint32_t fgSlot_ = 0; uint64_t fgFrame_ = 0, fgSlotFrame_[64] = {}; float fgOut_[4] = {0, 0, 0, 0}; bool fgValid_ = false;
    bool legacyY_ = false, noTrailBarrier_ = false; float ent_[kMaxEnt * 16] = {}; int nEnt_ = 0; std::vector<float> part_; int nPart_ = 0;
    VkBuffer pbuf_ = nullptr; VkDeviceMemory pmem_ = nullptr; void* pptr_ = nullptr;   // ring of particle slots (same slot index as the uniform ring)
    VkCtx ctx_{}; Fns* f_ = nullptr; GpuBridge* gb_ = nullptr;
    VkDescriptorSetLayout dsl_ = nullptr; VkPipelineLayout pl_ = nullptr; VkPipeline pipe_ = nullptr; VkShaderModule sm_ = nullptr; VkDescriptorPool dp_ = nullptr; VkDescriptorSet ds_ = nullptr;
    VkSampler samp_ = nullptr; VkImageView depthView_ = nullptr, sharedDepthView_ = nullptr, sharedMvView_ = nullptr; VkBuffer ubo_ = nullptr; VkDeviceMemory uboMem_ = nullptr; void* uboPtr_ = nullptr; uint32_t slot_ = 0;   // ring of uniform slots (dynamic offset): the CPU must never overwrite parameters a still-queued dispatch will read
    VkSemaphore sem_ = nullptr; uint32_t w_ = 0, h_ = 0;
};

}  // namespace fsr4

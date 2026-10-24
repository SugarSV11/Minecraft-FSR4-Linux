// Neural corrector inference engine (EXPERIMENTAL, docs/NEURAL_INFERENCE.md): one fused compute dispatch (shaders/neural_sel.comp) that evaluates a 4-layer 3x3 selection CNN on 16x16 tiles
// inside Minecraft's Vulkan device (or any VkDevice). Weights come from tools/ai/neural/export_weights.py (float32). No CPU work per frame.
#pragma once
#include <string>
#include <vector>
#include "gpu_bridge.hpp"

namespace fsr4 {

class NeuralPass {
public:
    ~NeuralPass() { release(); }
    // channels = 8 or 16 (hidden width of the exported network). Creates pipeline + weight buffer (host visible, uploaded once).
    bool init(const VkCtx& ctx, int channels, const float* weights, size_t nFloats, std::string* err);
    // Inputs (all sampled with texelFetch, GENERAL layout): G, A, B, C0, C1, F, M (rgba8-like, 1 texel per pixel); out: rgba8 storage image; tileFlags: optional storage buffer of uint32 per 16x16 tile.
    bool bind(VkImageView g, VkImageView a, VkImageView b, VkImageView c0, VkImageView c1, VkImageView f, VkImageView m, VkImageView out, VkBuffer tileFlags, uint32_t w, uint32_t h);
    void record(VkCommandBuffer cb, float t, bool useTiles);   // no barriers: the caller orders the inputs before and the output after
    size_t weightFloats() const { return nW_; }
    uint64_t deviceBytes() const { return bytes_; }
    void release();
    struct Fns;
private:
    VkCtx ctx_{}; Fns* f_ = nullptr; int c_ = 16; uint32_t w_ = 0, h_ = 0; size_t nW_ = 0; uint64_t bytes_ = 0;
    VkDescriptorSetLayout dsl_ = nullptr; VkPipelineLayout pl_ = nullptr; VkPipeline pipe_ = nullptr; VkShaderModule sm_ = nullptr; VkDescriptorPool dp_ = nullptr; VkDescriptorSet ds_ = nullptr; VkSampler nea_ = nullptr;
    VkBuffer wbuf_ = nullptr, dummy_ = nullptr; VkDeviceMemory wmem_ = nullptr, dmem_ = nullptr;
};

}  // namespace fsr4

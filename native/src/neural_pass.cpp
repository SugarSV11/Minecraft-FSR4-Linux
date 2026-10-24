#include "neural_pass.hpp"
#include <cstring>
#include "../generated/neural_sel_spv.h"
#include "../generated/neural_sel8_spv.h"

namespace fsr4 {

struct NeuralPass::Fns {
#define F(n) PFN_##n n = nullptr;
    F(vkCreateDescriptorSetLayout) F(vkDestroyDescriptorSetLayout) F(vkCreatePipelineLayout) F(vkDestroyPipelineLayout) F(vkCreateShaderModule) F(vkDestroyShaderModule)
    F(vkCreateComputePipelines) F(vkDestroyPipeline) F(vkCreateDescriptorPool) F(vkDestroyDescriptorPool) F(vkAllocateDescriptorSets) F(vkUpdateDescriptorSets)
    F(vkCreateSampler) F(vkDestroySampler) F(vkCreateBuffer) F(vkDestroyBuffer) F(vkGetBufferMemoryRequirements) F(vkBindBufferMemory) F(vkAllocateMemory) F(vkFreeMemory) F(vkMapMemory) F(vkUnmapMemory)
    F(vkCmdBindPipeline) F(vkCmdBindDescriptorSets) F(vkCmdDispatch) F(vkCmdPushConstants) F(vkGetPhysicalDeviceMemoryProperties)
#undef F
};

static int memType(NeuralPass::Fns* f, VkPhysicalDevice phys, uint32_t bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp; f->vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return (int)i;
    return -1;
}

bool NeuralPass::init(const VkCtx& c, int channels, const float* weights, size_t nFloats, std::string* err) {
    release(); ctx_ = c; f_ = new Fns(); c_ = channels;
    auto gd = (PFN_vkGetDeviceProcAddr)c.gipa(c.instance, "vkGetDeviceProcAddr");
#define LD(n) f_->n = (PFN_##n)gd(c.device, #n); if (!f_->n) { if (err) *err = "missing Vulkan entry point " #n; return false; }
    LD(vkCreateDescriptorSetLayout) LD(vkDestroyDescriptorSetLayout) LD(vkCreatePipelineLayout) LD(vkDestroyPipelineLayout) LD(vkCreateShaderModule) LD(vkDestroyShaderModule)
    LD(vkCreateComputePipelines) LD(vkDestroyPipeline) LD(vkCreateDescriptorPool) LD(vkDestroyDescriptorPool) LD(vkAllocateDescriptorSets) LD(vkUpdateDescriptorSets)
    LD(vkCreateSampler) LD(vkDestroySampler) LD(vkCreateBuffer) LD(vkDestroyBuffer) LD(vkGetBufferMemoryRequirements) LD(vkBindBufferMemory) LD(vkAllocateMemory) LD(vkFreeMemory) LD(vkMapMemory) LD(vkUnmapMemory)
    LD(vkCmdBindPipeline) LD(vkCmdBindDescriptorSets) LD(vkCmdDispatch) LD(vkCmdPushConstants)
#undef LD
    f_->vkGetPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties)c.gipa(c.instance, "vkGetPhysicalDeviceMemoryProperties");
    if (channels != 8 && channels != 16) { if (err) *err = "channels must be 8 or 16"; return false; }
    size_t expect = (size_t)channels * 22 * 9 + channels + 2 * ((size_t)channels * channels * 9 + channels) + (size_t)6 * channels * 9 + 6;
    if (nFloats != expect) { if (err) *err = "weight blob has " + std::to_string(nFloats) + " floats, expected " + std::to_string(expect); return false; }
    nW_ = nFloats;
    VkDescriptorSetLayoutBinding b[10] = {};
    for (int i = 0; i < 7; ++i) b[i] = {(uint32_t)i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    b[7] = {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}; b[8] = {8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}; b[9] = {9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; dli.bindingCount = 10; dli.pBindings = b;
    if (f_->vkCreateDescriptorSetLayout(c.device, &dli, nullptr, &dsl_)) { if (err) *err = "neural descriptor layout failed"; return false; }
    VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 32};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pli.setLayoutCount = 1; pli.pSetLayouts = &dsl_; pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcr;
    if (f_->vkCreatePipelineLayout(c.device, &pli, nullptr, &pl_)) { if (err) *err = "neural pipeline layout failed"; return false; }
    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; smi.codeSize = channels == 8 ? k_neural_sel8_spv_size : k_neural_sel_spv_size; smi.pCode = channels == 8 ? k_neural_sel8_spv : k_neural_sel_spv;
    if (f_->vkCreateShaderModule(c.device, &smi, nullptr, &sm_)) { if (err) *err = "neural shader module failed"; return false; }
    VkComputePipelineCreateInfo cpi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; cpi.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, sm_, "main", nullptr}; cpi.layout = pl_;
    if (f_->vkCreateComputePipelines(c.device, nullptr, 1, &cpi, nullptr, &pipe_)) { if (err) *err = "neural compute pipeline failed"; return false; }
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; si.magFilter = si.minFilter = VK_FILTER_NEAREST; si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (f_->vkCreateSampler(c.device, &si, nullptr, &nea_)) { if (err) *err = "neural sampler failed"; return false; }
    VkDescriptorPoolSize ps[3] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 7}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1}};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dpi.maxSets = 1; dpi.poolSizeCount = 3; dpi.pPoolSizes = ps;
    if (f_->vkCreateDescriptorPool(c.device, &dpi, nullptr, &dp_)) { if (err) *err = "neural descriptor pool failed"; return false; }
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; dai.descriptorPool = dp_; dai.descriptorSetCount = 1; dai.pSetLayouts = &dsl_;
    if (f_->vkAllocateDescriptorSets(c.device, &dai, &ds_)) { if (err) *err = "neural descriptor set failed"; return false; }
    auto mkbuf = [&](VkDeviceSize size, VkBuffer* buf, VkDeviceMemory* mem, void** map) {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size = size; bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; if (f_->vkCreateBuffer(c.device, &bi, nullptr, buf)) return false;
        VkMemoryRequirements mr; f_->vkGetBufferMemoryRequirements(c.device, *buf, &mr); int mt = memType(f_, c.phys, mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mai.allocationSize = mr.size; mai.memoryTypeIndex = mt < 0 ? 0 : (uint32_t)mt; bytes_ += mr.size;
        return mt >= 0 && !f_->vkAllocateMemory(c.device, &mai, nullptr, mem) && !f_->vkBindBufferMemory(c.device, *buf, *mem, 0) && !f_->vkMapMemory(c.device, *mem, 0, size, 0, map); };
    void* p = nullptr; if (!mkbuf(nFloats * 4, &wbuf_, &wmem_, &p)) { if (err) *err = "neural weight buffer failed"; return false; }
    memcpy(p, weights, nFloats * 4); f_->vkUnmapMemory(c.device, wmem_);
    void* q = nullptr; if (!mkbuf(256, &dummy_, &dmem_, &q)) { if (err) *err = "neural dummy buffer failed"; return false; } memset(q, 0, 256); f_->vkUnmapMemory(c.device, dmem_);
    return true;
}

bool NeuralPass::bind(VkImageView g, VkImageView a, VkImageView b, VkImageView c0, VkImageView c1, VkImageView fv, VkImageView m, VkImageView out, VkBuffer tileFlags, uint32_t w, uint32_t h) {
    if (!f_ || !ds_) return false; w_ = w; h_ = h;
    VkImageView v[7] = {g, a, b, c0, c1, fv, m}; VkDescriptorImageInfo di[8]; VkWriteDescriptorSet wr[10] = {};
    for (int i = 0; i < 7; ++i) { di[i] = {nea_, v[i], VK_IMAGE_LAYOUT_GENERAL}; wr[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; wr[i].pImageInfo = &di[i]; }
    di[7] = {nullptr, out, VK_IMAGE_LAYOUT_GENERAL}; VkDescriptorBufferInfo bw{wbuf_, 0, VK_WHOLE_SIZE}, bt{tileFlags ? tileFlags : dummy_, 0, VK_WHOLE_SIZE};
    wr[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; wr[7].pBufferInfo = &bw; wr[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; wr[8].pImageInfo = &di[7]; wr[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; wr[9].pBufferInfo = &bt;
    for (int i = 0; i < 10; ++i) { wr[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; wr[i].dstSet = ds_; wr[i].dstBinding = (uint32_t)i; wr[i].descriptorCount = 1; }
    f_->vkUpdateDescriptorSets(ctx_.device, 10, wr, 0, nullptr); return true;
}

void NeuralPass::record(VkCommandBuffer cb, float t, bool useTiles) {
    struct { int32_t dw, dh, tilesX; float t; int32_t useTiles, p0, p1, p2; } pc = {(int32_t)w_, (int32_t)h_, (int32_t)((w_ + 15) / 16), t, useTiles ? 1 : 0, 0, 0, 0};
    f_->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe_); f_->vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl_, 0, 1, &ds_, 0, nullptr);
    f_->vkCmdPushConstants(cb, pl_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, &pc); f_->vkCmdDispatch(cb, (w_ + 15) / 16, (h_ + 15) / 16, 1);
}

void NeuralPass::release() {
    if (!f_) return; VkDevice d = ctx_.device;
    if (dp_) f_->vkDestroyDescriptorPool(d, dp_, nullptr); if (pipe_) f_->vkDestroyPipeline(d, pipe_, nullptr); if (sm_) f_->vkDestroyShaderModule(d, sm_, nullptr); if (pl_) f_->vkDestroyPipelineLayout(d, pl_, nullptr);
    if (dsl_) f_->vkDestroyDescriptorSetLayout(d, dsl_, nullptr); if (nea_) f_->vkDestroySampler(d, nea_, nullptr);
    if (wbuf_) f_->vkDestroyBuffer(d, wbuf_, nullptr); if (wmem_) f_->vkFreeMemory(d, wmem_, nullptr); if (dummy_) f_->vkDestroyBuffer(d, dummy_, nullptr); if (dmem_) f_->vkFreeMemory(d, dmem_, nullptr);
    dp_ = nullptr; pipe_ = nullptr; sm_ = nullptr; pl_ = nullptr; dsl_ = nullptr; nea_ = nullptr; wbuf_ = nullptr; wmem_ = nullptr; dummy_ = nullptr; dmem_ = nullptr; ds_ = nullptr; delete f_; f_ = nullptr; bytes_ = 0;
}

}  // namespace fsr4

#include "prep_pass.hpp"
#include <cstring>
#include <cstdlib>
#include "../generated/fsr4_prep_spv.h"

namespace fsr4 {

static constexpr uint32_t kSlots = 16;
static constexpr uint32_t kPStride = PrepPass::kMaxParticles * 32;   // per-slot particle storage: 32 B per particle
static constexpr uint32_t kStride = 4096;   // per-slot UBO: 224 B params + up to 48 entities x 64 B   // >= (submits in flight + frames queued ahead) x 2 passes

struct PrepPass::Fns {
#define F(n) PFN_##n n = nullptr;
    F(vkCreateDescriptorSetLayout) F(vkDestroyDescriptorSetLayout) F(vkCreatePipelineLayout) F(vkDestroyPipelineLayout) F(vkCreateShaderModule) F(vkDestroyShaderModule)
    F(vkCreateComputePipelines) F(vkDestroyPipeline) F(vkCreateDescriptorPool) F(vkDestroyDescriptorPool) F(vkAllocateDescriptorSets) F(vkUpdateDescriptorSets)
    F(vkCreateImageView) F(vkDestroyImageView) F(vkCreateSampler) F(vkDestroySampler) F(vkCreateBuffer) F(vkDestroyBuffer) F(vkGetBufferMemoryRequirements)
    F(vkAllocateMemory) F(vkFreeMemory) F(vkBindBufferMemory) F(vkMapMemory) F(vkCmdBindPipeline) F(vkCmdBindDescriptorSets) F(vkCmdDispatch) F(vkCmdPipelineBarrier)
    F(vkCmdCopyImage) F(vkCmdPushConstants) F(vkCreateSemaphore) F(vkDestroySemaphore) F(vkWaitSemaphores) F(vkGetPhysicalDeviceMemoryProperties)
    F(vkCreateQueryPool) F(vkDestroyQueryPool) F(vkCmdResetQueryPool) F(vkCmdWriteTimestamp) F(vkGetQueryPoolResults) F(vkGetPhysicalDeviceProperties)
#undef F
};

bool PrepPass::init(const VkCtx& c, std::string* err) {
    ctx_ = c; delete f_; f_ = new Fns();
    auto gd = (PFN_vkGetDeviceProcAddr)c.gipa(c.instance, "vkGetDeviceProcAddr");
#define LD(n) f_->n = (PFN_##n)gd(c.device, #n); if (!f_->n) { if (err) *err = "missing Vulkan entry point " #n; return false; }
    LD(vkCreateDescriptorSetLayout) LD(vkDestroyDescriptorSetLayout) LD(vkCreatePipelineLayout) LD(vkDestroyPipelineLayout) LD(vkCreateShaderModule) LD(vkDestroyShaderModule)
    LD(vkCreateComputePipelines) LD(vkDestroyPipeline) LD(vkCreateDescriptorPool) LD(vkDestroyDescriptorPool) LD(vkAllocateDescriptorSets) LD(vkUpdateDescriptorSets)
    LD(vkCreateImageView) LD(vkDestroyImageView) LD(vkCreateSampler) LD(vkDestroySampler) LD(vkCreateBuffer) LD(vkDestroyBuffer) LD(vkGetBufferMemoryRequirements)
    LD(vkAllocateMemory) LD(vkFreeMemory) LD(vkBindBufferMemory) LD(vkMapMemory) LD(vkCmdBindPipeline) LD(vkCmdBindDescriptorSets) LD(vkCmdDispatch) LD(vkCmdPipelineBarrier)
    LD(vkCmdCopyImage) LD(vkCmdPushConstants) LD(vkCreateSemaphore) LD(vkDestroySemaphore) LD(vkWaitSemaphores)
#undef LD
    f_->vkGetPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties)c.gipa(c.instance, "vkGetPhysicalDeviceMemoryProperties");
    tsOn_ = std::getenv("FSR4_GPU_TS") != nullptr;
    legacyY_ = std::getenv("FSR4_MV_LEGACY_Y") != nullptr; noTrailBarrier_ = std::getenv("FSR4_NO_PREP_BARRIER") != nullptr;   // before/after measurements only
    if (tsOn_) {
        f_->vkCreateQueryPool = (PFN_vkCreateQueryPool)gd(c.device, "vkCreateQueryPool"); f_->vkDestroyQueryPool = (PFN_vkDestroyQueryPool)gd(c.device, "vkDestroyQueryPool");
        f_->vkCmdResetQueryPool = (PFN_vkCmdResetQueryPool)gd(c.device, "vkCmdResetQueryPool"); f_->vkCmdWriteTimestamp = (PFN_vkCmdWriteTimestamp)gd(c.device, "vkCmdWriteTimestamp");
        f_->vkGetQueryPoolResults = (PFN_vkGetQueryPoolResults)gd(c.device, "vkGetQueryPoolResults");
        f_->vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)c.gipa(c.instance, "vkGetPhysicalDeviceProperties");
        VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO}; qi.queryType = VK_QUERY_TYPE_TIMESTAMP; qi.queryCount = kSlots * 8;
        VkPhysicalDeviceProperties pr; f_->vkGetPhysicalDeviceProperties(c.phys, &pr); tsPeriod_ = pr.limits.timestampPeriod;
        if (!f_->vkCreateQueryPool || !f_->vkCmdResetQueryPool || !f_->vkCmdWriteTimestamp || !f_->vkGetQueryPoolResults || f_->vkCreateQueryPool(c.device, &qi, nullptr, &qp_) != VK_SUCCESS) tsOn_ = false;
    }
    VkDescriptorSetLayoutBinding b[5] = {};
    b[0] = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    b[1] = {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    b[2] = {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    b[3] = {3, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    b[4] = {4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; dli.bindingCount = 5; dli.pBindings = b;
    if (f_->vkCreateDescriptorSetLayout(c.device, &dli, nullptr, &dsl_)) { if (err) *err = "descriptor set layout failed"; return false; }
    VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 4};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pli.setLayoutCount = 1; pli.pSetLayouts = &dsl_; pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcr;
    if (f_->vkCreatePipelineLayout(c.device, &pli, nullptr, &pl_)) { if (err) *err = "pipeline layout failed"; return false; }
    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; smi.codeSize = k_fsr4_prep_spv_size; smi.pCode = k_fsr4_prep_spv;
    if (f_->vkCreateShaderModule(c.device, &smi, nullptr, &sm_)) { if (err) *err = "shader module failed"; return false; }
    VkComputePipelineCreateInfo cpi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; cpi.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, sm_, "main", nullptr}; cpi.layout = pl_;
    if (f_->vkCreateComputePipelines(c.device, nullptr, 1, &cpi, nullptr, &pipe_)) { if (err) *err = "compute pipeline failed"; return false; }
    VkDescriptorPoolSize ps[4] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1}};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dpi.maxSets = 1; dpi.poolSizeCount = 4; dpi.pPoolSizes = ps;
    if (f_->vkCreateDescriptorPool(c.device, &dpi, nullptr, &dp_)) { if (err) *err = "descriptor pool failed"; return false; }
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; dai.descriptorPool = dp_; dai.descriptorSetCount = 1; dai.pSetLayouts = &dsl_;
    if (f_->vkAllocateDescriptorSets(c.device, &dai, &ds_)) { if (err) *err = "descriptor set alloc failed"; return false; }
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; si.magFilter = si.minFilter = VK_FILTER_NEAREST; si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (f_->vkCreateSampler(c.device, &si, nullptr, &samp_)) { if (err) *err = "sampler failed"; return false; }
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size = kStride * kSlots; bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (f_->vkCreateBuffer(c.device, &bi, nullptr, &ubo_)) { if (err) *err = "ubo failed"; return false; }
    VkMemoryRequirements mr; f_->vkGetBufferMemoryRequirements(c.device, ubo_, &mr); VkPhysicalDeviceMemoryProperties mp; f_->vkGetPhysicalDeviceMemoryProperties(c.phys, &mp); int mt = -1;
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) if ((mr.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { mt = (int)i; break; }
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mai.allocationSize = mr.size; mai.memoryTypeIndex = (uint32_t)mt;
    if (mt < 0 || f_->vkAllocateMemory(c.device, &mai, nullptr, &uboMem_) || f_->vkBindBufferMemory(c.device, ubo_, uboMem_, 0) || f_->vkMapMemory(c.device, uboMem_, 0, kStride * kSlots, 0, &uboPtr_)) { if (err) *err = "ubo memory failed"; return false; }
    {   // particle ring (storage buffer, host visible)
        VkBufferCreateInfo pb{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; pb.size = (VkDeviceSize)kPStride * kSlots; pb.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        VkMemoryRequirements pr; int pm = -1;
        if (f_->vkCreateBuffer(c.device, &pb, nullptr, &pbuf_)) { if (err) *err = "particle buffer failed"; return false; }
        f_->vkGetBufferMemoryRequirements(c.device, pbuf_, &pr);
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) if ((pr.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { pm = (int)i; break; }
        VkMemoryAllocateInfo pa{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; pa.allocationSize = pr.size; pa.memoryTypeIndex = (uint32_t)pm;
        if (pm < 0 || f_->vkAllocateMemory(c.device, &pa, nullptr, &pmem_) || f_->vkBindBufferMemory(c.device, pbuf_, pmem_, 0) || f_->vkMapMemory(c.device, pmem_, 0, (VkDeviceSize)kPStride * kSlots, 0, &pptr_)) { if (err) *err = "particle buffer memory failed"; return false; }
    }
    VkSemaphoreTypeCreateInfo ti{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO}; ti.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE; ti.initialValue = 0; VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &ti};
    if (f_->vkCreateSemaphore(c.device, &sci, nullptr, &sem_)) { if (err) *err = "timeline semaphore failed"; return false; }
    return true;
}

void PrepPass::unbind() {
    if (!f_) return;
    for (VkImageView* v : {&depthView_, &sharedDepthView_, &sharedMvView_}) if (*v) { f_->vkDestroyImageView(ctx_.device, *v, nullptr); *v = nullptr; }
    gb_ = nullptr;
}

bool PrepPass::bind(GpuBridge* gb, VkImage mcDepth, std::string* err) {
    unbind(); gb_ = gb; w_ = gb->renderW(); h_ = gb->renderH();
    auto mk = [&](VkImage img, VkFormat fmt, VkImageAspectFlags aspect, VkImageView* out) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = img; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = fmt; vi.subresourceRange = {aspect, 0, 1, 0, 1};
        return f_->vkCreateImageView(ctx_.device, &vi, nullptr, out) == VK_SUCCESS; };
    if (!mk(mcDepth, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT, &depthView_) || !mk(gb->image(1), VK_FORMAT_R32_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, &sharedDepthView_) || !mk(gb->image(2), VK_FORMAT_R16G16_SFLOAT, VK_IMAGE_ASPECT_COLOR_BIT, &sharedMvView_)) { if (err) *err = "image view creation failed"; unbind(); return false; }
    VkDescriptorImageInfo di[3] = {{samp_, depthView_, VK_IMAGE_LAYOUT_GENERAL}, {nullptr, sharedDepthView_, VK_IMAGE_LAYOUT_GENERAL}, {nullptr, sharedMvView_, VK_IMAGE_LAYOUT_GENERAL}};
    VkDescriptorBufferInfo bi{ubo_, 0, kStride}, pbi{pbuf_, 0, kPStride}; VkWriteDescriptorSet w[5] = {};
    for (int i = 0; i < 5; ++i) { w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[i].dstSet = ds_; w[i].dstBinding = i; w[i].descriptorCount = 1; }
    w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[0].pImageInfo = &di[0]; w[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w[1].pImageInfo = &di[1];
    w[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w[2].pImageInfo = &di[2]; w[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; w[3].pBufferInfo = &bi;
    w[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC; w[4].pBufferInfo = &pbi;
    f_->vkUpdateDescriptorSets(ctx_.device, 5, w, 0, nullptr); return true;
}

static void globalBarrier(PrepPass::Fns* f, VkCommandBuffer cb) {
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; mb.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    f->vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
}

static void copyColor(PrepPass::Fns* f, GpuBridge* gb, VkCommandBuffer cb, VkImage mcColor, uint32_t w, uint32_t h) {
    VkImageCopy c{}; c.srcSubresource = c.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; c.extent = {w, h, 1};
    f->vkCmdCopyImage(cb, mcColor, VK_IMAGE_LAYOUT_GENERAL, gb->image(0), VK_IMAGE_LAYOUT_GENERAL, 1, &c);
}
void PrepPass::ts(VkCommandBuffer cb, uint32_t idx) { if (tsOn_) f_->vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp_, slot_ * 4 + idx); }
void PrepPass::recordWorld(VkCommandBuffer cb, const float* m48, const float* camDelta3) {
    slot_ = (slot_ + 1) % kSlots;
    if (tsOn_) { f_->vkCmdResetQueryPool(cb, qp_, slot_ * 4, 4); tsSlotFrame_[slot_] = ++tsFrame_; }
    float* u = (float*)((char*)uboPtr_ + (size_t)slot_ * kStride); memcpy(u, m48, 48 * sizeof(float)); u[48] = camDelta3[0]; u[49] = camDelta3[1]; u[50] = camDelta3[2]; u[51] = 0; u[52] = (float)w_; u[53] = (float)h_; u[54] = (float)nEnt_; u[55] = legacyY_ ? 1.0f : 0.0f;
    if (nEnt_) memcpy(u + 56, ent_, (size_t)nEnt_ * 16 * sizeof(float));
    u[51] = (float)nPart_;
    if (nPart_) memcpy((char*)pptr_ + (size_t)slot_ * kPStride, part_.data(), (size_t)nPart_ * 32);
    globalBarrier(f_, cb); ts(cb, 0);
    uint32_t off[2] = {slot_ * kStride, slot_ * kPStride}, mode = 0;
    f_->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe_); f_->vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl_, 0, 1, &ds_, 2, off);
    f_->vkCmdPushConstants(cb, pl_, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &mode);
    f_->vkCmdDispatch(cb, (w_ + 7) / 8, (h_ + 7) / 8, 1);
    if (nPart_ > 0) {   // particles: after the whole-plane pass, before Minecraft clears the depth buffer
        globalBarrier(f_, cb); mode = 2;
        f_->vkCmdPushConstants(cb, pl_, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &mode);
        f_->vkCmdDispatch(cb, (uint32_t)nPart_, 1, 1);
    }
    // Trailing barrier (session 8 fix): this pass READS Minecraft's depth buffer, and Minecraft clears that buffer right after (before drawing the hand) in the SAME queue submission.
    // Without an execution dependency the clear could overlap the read (WAR hazard): depth/MV planes came out partially zero (64x64 tile checkerboards) in most frames; a submit boundary hid it.
    globalBarrier(f_, cb);
}
void PrepPass::setEntities(const float* data, int n) {
    nEnt_ = n < 0 ? 0 : (n > kMaxEnt ? kMaxEnt : n);
    if (nEnt_) memcpy(ent_, data, (size_t)nEnt_ * 16 * sizeof(float));
}
void PrepPass::setParticles(const float* data, int n) {
    nPart_ = n < 0 ? 0 : (n > kMaxParticles ? kMaxParticles : n);
    if (nPart_) { part_.assign(data, data + (size_t)nPart_ * 8); }
}
void PrepPass::recordHandAndColor(VkCommandBuffer cb, VkImage mcColor) {
    globalBarrier(f_, cb);
    uint32_t off[2] = {slot_ * kStride, slot_ * kPStride}, mode = 1;   // same slot as this frame's world pass (only the size is read in mode 1)
    f_->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe_); f_->vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl_, 0, 1, &ds_, 2, off);
    f_->vkCmdPushConstants(cb, pl_, VK_SHADER_STAGE_COMPUTE_BIT, 0, 4, &mode);
    f_->vkCmdDispatch(cb, (w_ + 7) / 8, (h_ + 7) / 8, 1);
    copyColor(f_, gb_, cb, mcColor, w_, h_);   // different images than the dispatch writes: no barrier in between
    globalBarrier(f_, cb); ts(cb, 1);
}
void PrepPass::record(VkCommandBuffer cb, VkImage mcColor, const float* m48, const float* camDelta3) {   // single-shot form (tests, prep_test): world pass + colour copy
    recordWorld(cb, m48, camDelta3);
    copyColor(f_, gb_, cb, mcColor, w_, h_);
    globalBarrier(f_, cb);
}

void PrepPass::recordCopyOut(VkCommandBuffer cb, VkImage dst) {
    globalBarrier(f_, cb); ts(cb, 2);
    VkImageCopy c{}; c.srcSubresource = c.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; c.extent = {gb_->outW(), gb_->outH(), 1};
    f_->vkCmdCopyImage(cb, gb_->image(3), VK_IMAGE_LAYOUT_GENERAL, dst, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
    globalBarrier(f_, cb); ts(cb, 3);
    pollTs();
}
void PrepPass::pollTs() {
    if (!tsOn_) return;
    // read the frame submitted 4 frames ago (results are complete by then in normal operation) and its predecessor for the gap
    uint32_t s = (slot_ + kSlots - 4) % kSlots, sp = (s + kSlots - 1) % kSlots; uint64_t a[4], b[4];
    if (tsSlotFrame_[s] == 0 || tsSlotFrame_[sp] + 1 != tsSlotFrame_[s]) return;
    if (f_->vkGetQueryPoolResults(ctx_.device, qp_, s * 4, 4, sizeof a, a, 8, VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) return;
    if (f_->vkGetQueryPoolResults(ctx_.device, qp_, sp * 4, 4, sizeof b, b, 8, VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) return;
    double k = tsPeriod_ * 1e-6;   // ticks -> ms
    tsOut_[0] = (float)((a[1] - a[0]) * k); tsOut_[1] = (float)((a[2] - a[1]) * k); tsOut_[2] = (float)((a[3] - a[2]) * k); tsOut_[3] = (float)((a[0] - b[3]) * k); tsValid_ = true;
}
void PrepPass::fgMark(VkCommandBuffer cb, int k) {
    if (!tsOn_) return;
    if (k == 0) { fgSlot_ = (fgSlot_ + 1) % kSlots; f_->vkCmdResetQueryPool(cb, qp_, kSlots * 4 + fgSlot_ * 4, 4); fgSlotFrame_[fgSlot_] = ++fgFrame_; }
    f_->vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp_, kSlots * 4 + fgSlot_ * 4 + (uint32_t)k);
    if (k == 3 && fgFrame_ > 5) {
        uint32_t s = (fgSlot_ + kSlots - 4) % kSlots, sp = (s + kSlots - 1) % kSlots; uint64_t a[4], b[4];
        if (fgSlotFrame_[s] == 0 || fgSlotFrame_[sp] + 1 != fgSlotFrame_[s]) return;
        if (f_->vkGetQueryPoolResults(ctx_.device, qp_, kSlots * 4 + s * 4, 4, sizeof a, a, 8, VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) return;
        if (f_->vkGetQueryPoolResults(ctx_.device, qp_, kSlots * 4 + sp * 4, 4, sizeof b, b, 8, VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) return;
        double kk = tsPeriod_ * 1e-6; fgOut_[0] = (float)((a[1] - a[0]) * kk); fgOut_[1] = (float)((a[2] - a[1]) * kk); fgOut_[2] = (float)((a[3] - a[2]) * kk); fgOut_[3] = (float)((a[0] - b[3]) * kk); fgValid_ = true;
    }
}
bool PrepPass::fgGpuTimes(float* out4) const { if (!tsOn_ || !fgValid_) return false; for (int i = 0; i < 4; ++i) out4[i] = fgOut_[i]; return true; }
bool PrepPass::gpuTimes(float* out4) const { if (!tsOn_ || !tsValid_) return false; for (int i = 0; i < 4; ++i) out4[i] = tsOut_[i]; return true; }

bool PrepPass::waitTimeline(uint64_t value, uint64_t timeoutNs) {
    VkSemaphoreWaitInfo wi{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO}; wi.semaphoreCount = 1; wi.pSemaphores = &sem_; wi.pValues = &value;
    return f_->vkWaitSemaphores(ctx_.device, &wi, timeoutNs) == VK_SUCCESS;
}

void PrepPass::destroy() {
    if (!f_) return; unbind();
    VkDevice d = ctx_.device;
    if (qp_) f_->vkDestroyQueryPool(d, qp_, nullptr); qp_ = nullptr; tsValid_ = false;
    if (pbuf_) f_->vkDestroyBuffer(d, pbuf_, nullptr); if (pmem_) f_->vkFreeMemory(d, pmem_, nullptr); pbuf_ = nullptr; pmem_ = nullptr; pptr_ = nullptr;
    if (sem_) f_->vkDestroySemaphore(d, sem_, nullptr); if (samp_) f_->vkDestroySampler(d, samp_, nullptr); if (ubo_) f_->vkDestroyBuffer(d, ubo_, nullptr); if (uboMem_) f_->vkFreeMemory(d, uboMem_, nullptr);
    if (dp_) f_->vkDestroyDescriptorPool(d, dp_, nullptr); if (pipe_) f_->vkDestroyPipeline(d, pipe_, nullptr); if (sm_) f_->vkDestroyShaderModule(d, sm_, nullptr);
    if (pl_) f_->vkDestroyPipelineLayout(d, pl_, nullptr); if (dsl_) f_->vkDestroyDescriptorSetLayout(d, dsl_, nullptr);
    sem_ = nullptr; samp_ = nullptr; ubo_ = nullptr; uboMem_ = nullptr; dp_ = nullptr; pipe_ = nullptr; sm_ = nullptr; pl_ = nullptr; dsl_ = nullptr; delete f_; f_ = nullptr;
}

}  // namespace fsr4

#include "fgx3_pass.hpp"
#include <cstring>
#include <cstdlib>
#include "../generated/fgx3_spv.h"
#include "../generated/fgx3i64_spv.h"

namespace fsr4 {

struct FgX3Pass::Fns {
#define F(n) PFN_##n n = nullptr;
    F(vkCreateDescriptorSetLayout) F(vkDestroyDescriptorSetLayout) F(vkCreatePipelineLayout) F(vkDestroyPipelineLayout) F(vkCreateShaderModule) F(vkDestroyShaderModule)
    F(vkCreateComputePipelines) F(vkDestroyPipeline) F(vkCreateDescriptorPool) F(vkDestroyDescriptorPool) F(vkAllocateDescriptorSets) F(vkUpdateDescriptorSets)
    F(vkCreateImage) F(vkDestroyImage) F(vkGetImageMemoryRequirements) F(vkBindImageMemory) F(vkCreateImageView) F(vkDestroyImageView) F(vkCreateSampler) F(vkDestroySampler)
    F(vkCreateBuffer) F(vkDestroyBuffer) F(vkGetBufferMemoryRequirements) F(vkBindBufferMemory) F(vkAllocateMemory) F(vkFreeMemory)
    F(vkCmdBindPipeline) F(vkCmdBindDescriptorSets) F(vkCmdDispatch) F(vkCmdPipelineBarrier) F(vkCmdCopyImage) F(vkCmdPushConstants)
    F(vkCreateCommandPool) F(vkDestroyCommandPool) F(vkAllocateCommandBuffers) F(vkBeginCommandBuffer) F(vkEndCommandBuffer) F(vkQueueSubmit) F(vkQueueWaitIdle) F(vkFreeCommandBuffers)
    F(vkCreateQueryPool) F(vkDestroyQueryPool) F(vkCmdResetQueryPool) F(vkCmdWriteTimestamp) F(vkGetQueryPoolResults) F(vkGetPhysicalDeviceProperties) F(vkGetPhysicalDeviceMemoryProperties)
#undef F
};

static bool g_i64 = false;
void FgX3Pass::setInt64(bool on) { g_i64 = on; }

bool FgX3Pass::init(const VkCtx& c, std::string* err) {
    destroy(); ctx_ = c; f_ = new Fns(); i64_ = g_i64 && !std::getenv("FSR4_X3_NO_INT64");
    auto gd = (PFN_vkGetDeviceProcAddr)c.gipa(c.instance, "vkGetDeviceProcAddr");
#define LD(n) f_->n = (PFN_##n)gd(c.device, #n); if (!f_->n) { if (err) *err = "missing Vulkan entry point " #n; return false; }
    LD(vkCreateDescriptorSetLayout) LD(vkDestroyDescriptorSetLayout) LD(vkCreatePipelineLayout) LD(vkDestroyPipelineLayout) LD(vkCreateShaderModule) LD(vkDestroyShaderModule)
    LD(vkCreateComputePipelines) LD(vkDestroyPipeline) LD(vkCreateDescriptorPool) LD(vkDestroyDescriptorPool) LD(vkAllocateDescriptorSets) LD(vkUpdateDescriptorSets)
    LD(vkCreateImage) LD(vkDestroyImage) LD(vkGetImageMemoryRequirements) LD(vkBindImageMemory) LD(vkCreateImageView) LD(vkDestroyImageView) LD(vkCreateSampler) LD(vkDestroySampler)
    LD(vkCreateBuffer) LD(vkDestroyBuffer) LD(vkGetBufferMemoryRequirements) LD(vkBindBufferMemory) LD(vkAllocateMemory) LD(vkFreeMemory)
    LD(vkCmdBindPipeline) LD(vkCmdBindDescriptorSets) LD(vkCmdDispatch) LD(vkCmdPipelineBarrier) LD(vkCmdCopyImage) LD(vkCmdPushConstants)
    LD(vkCreateCommandPool) LD(vkDestroyCommandPool) LD(vkAllocateCommandBuffers) LD(vkBeginCommandBuffer) LD(vkEndCommandBuffer) LD(vkQueueSubmit) LD(vkQueueWaitIdle) LD(vkFreeCommandBuffers)
#undef LD
    f_->vkGetPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties)c.gipa(c.instance, "vkGetPhysicalDeviceMemoryProperties");
    f_->vkGetPhysicalDeviceProperties = (PFN_vkGetPhysicalDeviceProperties)c.gipa(c.instance, "vkGetPhysicalDeviceProperties");
    tsOn_ = std::getenv("FSR4_GPU_TS") != nullptr; dbg_ = std::getenv("FSR4_X3_DEBUGVIEW") != nullptr; dbgMode_ = dbg_ ? atoi(std::getenv("FSR4_X3_DEBUGVIEW")) : 0; if (dbg_ && dbgMode_ == 0) dbgMode_ = 1; flipY_ = std::getenv("FSR4_X3_FLIPY") != nullptr; skip_ = std::getenv("FSR4_X3_SKIP") ? (uint32_t)atoi(std::getenv("FSR4_X3_SKIP")) : 0; variant_ = std::getenv("FSR4_X3_VARIANT") ? atoi(std::getenv("FSR4_X3_VARIANT")) & 7 : 2;   // 2 = measured best (docs/VISUAL_QUALITY.md session 9) dbg_ = std::getenv("FSR4_X3_DEBUGVIEW") != nullptr;
    if (tsOn_) {
        f_->vkCreateQueryPool = (PFN_vkCreateQueryPool)gd(c.device, "vkCreateQueryPool"); f_->vkDestroyQueryPool = (PFN_vkDestroyQueryPool)gd(c.device, "vkDestroyQueryPool");
        f_->vkCmdResetQueryPool = (PFN_vkCmdResetQueryPool)gd(c.device, "vkCmdResetQueryPool"); f_->vkCmdWriteTimestamp = (PFN_vkCmdWriteTimestamp)gd(c.device, "vkCmdWriteTimestamp");
        f_->vkGetQueryPoolResults = (PFN_vkGetQueryPoolResults)gd(c.device, "vkGetQueryPoolResults");
        VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO}; qi.queryType = VK_QUERY_TYPE_TIMESTAMP; qi.queryCount = 8 * 2;
        VkPhysicalDeviceProperties pr; f_->vkGetPhysicalDeviceProperties(c.phys, &pr); tsPeriod_ = pr.limits.timestampPeriod;
        if (!f_->vkCreateQueryPool || !f_->vkCmdResetQueryPool || !f_->vkCmdWriteTimestamp || !f_->vkGetQueryPoolResults || f_->vkCreateQueryPool(c.device, &qi, nullptr, &qp_) != VK_SUCCESS) tsOn_ = false;
    }
    prof_ = std::getenv("FSR4_X3_PROFILE") != nullptr && tsOn_;
    if (prof_) { VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO}; qi.queryType = VK_QUERY_TYPE_TIMESTAMP; qi.queryCount = 5; if (f_->vkCreateQueryPool(c.device, &qi, nullptr, &qpP_) != VK_SUCCESS) prof_ = false; }
    VkDescriptorSetLayoutBinding b[9] = {};
    for (int i = 0; i < 6; ++i) b[i] = {(uint32_t)i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    b[6] = {6, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}; b[7] = {7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    b[8] = {8, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; dli.bindingCount = 9; dli.pBindings = b;
    if (f_->vkCreateDescriptorSetLayout(c.device, &dli, nullptr, &dsl_)) { if (err) *err = "x3 descriptor set layout failed"; return false; }
    VkPushConstantRange pcr{VK_SHADER_STAGE_COMPUTE_BIT, 0, 32};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pli.setLayoutCount = 1; pli.pSetLayouts = &dsl_; pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcr;
    if (f_->vkCreatePipelineLayout(c.device, &pli, nullptr, &pl_)) { if (err) *err = "x3 pipeline layout failed"; return false; }
    VkShaderModuleCreateInfo smi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; smi.codeSize = i64_ ? k_fgx3i64_spv_size : k_fgx3_spv_size; smi.pCode = i64_ ? k_fgx3i64_spv : k_fgx3_spv;
    if (f_->vkCreateShaderModule(c.device, &smi, nullptr, &sm_)) { if (err) *err = "x3 shader module failed"; return false; }
    VkComputePipelineCreateInfo cpi{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; cpi.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_COMPUTE_BIT, sm_, "main", nullptr}; cpi.layout = pl_;
    if (f_->vkCreateComputePipelines(c.device, nullptr, 1, &cpi, nullptr, &pipe_)) { if (err) *err = "x3 compute pipeline failed"; return false; }
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.magFilter = si.minFilter = VK_FILTER_LINEAR; if (f_->vkCreateSampler(c.device, &si, nullptr, &lin_)) { if (err) *err = "x3 sampler failed"; return false; }
    si.magFilter = si.minFilter = VK_FILTER_NEAREST; if (f_->vkCreateSampler(c.device, &si, nullptr, &nea_)) { if (err) *err = "x3 sampler failed"; return false; }
    VkCommandPoolCreateInfo cpo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cpo.queueFamilyIndex = c.queueFamily; cpo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    if (f_->vkCreateCommandPool(c.device, &cpo, nullptr, &pool_)) { if (err) *err = "x3 command pool failed"; return false; }
    return true;
}

static int memType(FgX3Pass::Fns* f, VkPhysicalDevice phys, uint32_t bits, VkMemoryPropertyFlags want) {
    VkPhysicalDeviceMemoryProperties mp; f->vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return (int)i;
    return -1;
}

bool FgX3Pass::alloc(uint32_t dw, uint32_t dh, uint32_t rw, uint32_t rh, VkImage depth, VkImage mv, std::string* err) {
    release(); if (!f_) { if (err) *err = "x3 not initialised"; return false; }
    dw_ = dw; dh_ = dh; rw_ = rw; rh_ = rh; depthIn_ = depth; VkDevice d = ctx_.device;
    struct Spec { VkFormat fmt; VkImageUsageFlags use; uint32_t w, h; } sp[5] = {
        {VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, dw, dh},
        {VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, dw, dh},
        {VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, dw, dh},
        {VK_FORMAT_R32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, rw, rh},
        {VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, dw, dh}};
    auto fail = [&](const char* m) { if (err) *err = m; release(); return false; };
    for (int i = 0; i < 5; ++i) {
        VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ic.imageType = VK_IMAGE_TYPE_2D; ic.format = sp[i].fmt; ic.extent = {sp[i].w, sp[i].h, 1}; ic.mipLevels = 1; ic.arrayLayers = 1;
        ic.samples = VK_SAMPLE_COUNT_1_BIT; ic.tiling = VK_IMAGE_TILING_OPTIMAL; ic.usage = sp[i].use; ic.sharingMode = VK_SHARING_MODE_EXCLUSIVE; ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (f_->vkCreateImage(d, &ic, nullptr, &img_[i])) return fail("x3 image creation failed");
        VkMemoryRequirements mr; f_->vkGetImageMemoryRequirements(d, img_[i], &mr); int mt = memType(f_, ctx_.phys, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mai.allocationSize = mr.size; mai.memoryTypeIndex = mt < 0 ? 0 : (uint32_t)mt;
        if (mt < 0 || f_->vkAllocateMemory(d, &mai, nullptr, &mem_[i]) || f_->vkBindImageMemory(d, img_[i], mem_[i], 0)) return fail("x3 image memory failed");
        bytes_ += mr.size;
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = img_[i]; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = sp[i].fmt; vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (f_->vkCreateImageView(d, &vi, nullptr, &view_[i])) return fail("x3 image view failed");
    }
    for (int i = 0; i < 2; ++i) {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size = (VkDeviceSize)dw * dh * ((i64_ && i == 0) ? 8 : 4); bi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        if (f_->vkCreateBuffer(d, &bi, nullptr, &buf_[i])) return fail("x3 buffer failed");
        VkMemoryRequirements mr; f_->vkGetBufferMemoryRequirements(d, buf_[i], &mr); int mt = memType(f_, ctx_.phys, mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mai.allocationSize = mr.size; mai.memoryTypeIndex = mt < 0 ? 0 : (uint32_t)mt;
        if (mt < 0 || f_->vkAllocateMemory(d, &mai, nullptr, &bmem_[i]) || f_->vkBindBufferMemory(d, buf_[i], bmem_[i], 0)) return fail("x3 buffer memory failed");
        bytes_ += mr.size;
    }
    VkImageViewCreateInfo vd{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vd.image = depth; vd.viewType = VK_IMAGE_VIEW_TYPE_2D; vd.format = VK_FORMAT_R32_SFLOAT; vd.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (f_->vkCreateImageView(d, &vd, nullptr, &depthInView_)) return fail("x3 depth view failed");
    VkImageViewCreateInfo vm = vd; vm.image = mv; vm.format = VK_FORMAT_R16G16_SFLOAT; if (f_->vkCreateImageView(d, &vm, nullptr, &mvView_)) return fail("x3 mv view failed");
    VkDescriptorPoolSize ps[3] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 12}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4}, {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2}};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dpi.maxSets = 2; dpi.poolSizeCount = 3; dpi.pPoolSizes = ps;
    if (f_->vkCreateDescriptorPool(d, &dpi, nullptr, &dp_)) return fail("x3 descriptor pool failed");
    VkDescriptorSetLayout ls[2] = {dsl_, dsl_}; VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; dai.descriptorPool = dp_; dai.descriptorSetCount = 2; dai.pSetLayouts = ls;
    if (f_->vkAllocateDescriptorSets(d, &dai, ds_)) return fail("x3 descriptor set alloc failed");
    for (int par = 0; par < 2; ++par) {   // parity: which colour image plays "current"
        VkDescriptorImageInfo di[9] = {{lin_, view_[par], VK_IMAGE_LAYOUT_GENERAL}, {lin_, view_[1 - par], VK_IMAGE_LAYOUT_GENERAL}, {nea_, depthInView_, VK_IMAGE_LAYOUT_GENERAL}, {nea_, view_[3], VK_IMAGE_LAYOUT_GENERAL},
            {nea_, mvView_, VK_IMAGE_LAYOUT_GENERAL}, {nea_, view_[2], VK_IMAGE_LAYOUT_GENERAL}, {}, {}, {nullptr, view_[4], VK_IMAGE_LAYOUT_GENERAL}};
        VkDescriptorBufferInfo bi2[2] = {{buf_[0], 0, VK_WHOLE_SIZE}, {buf_[1], 0, VK_WHOLE_SIZE}}; VkWriteDescriptorSet w[9] = {};
        for (int i = 0; i < 9; ++i) { w[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; w[i].dstSet = ds_[par]; w[i].dstBinding = (uint32_t)i; w[i].descriptorCount = 1; }
        for (int i = 0; i < 6; ++i) { w[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[i].pImageInfo = &di[i]; }
        w[6].descriptorType = w[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[6].pBufferInfo = &bi2[0]; w[7].pBufferInfo = &bi2[1];
        w[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; w[8].pImageInfo = &di[8];
        f_->vkUpdateDescriptorSets(d, 9, w, 0, nullptr);
    }
    // one-off transition of every private image to GENERAL (the only layout used afterwards)
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cai.commandPool = pool_; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1; VkCommandBuffer cb;
    if (f_->vkAllocateCommandBuffers(d, &cai, &cb)) return fail("x3 command buffer failed");
    VkCommandBufferBeginInfo bg{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bg.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; f_->vkBeginCommandBuffer(cb, &bg);
    VkImageMemoryBarrier ib[5] = {}; for (int i = 0; i < 5; ++i) { ib[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER; ib[i].newLayout = VK_IMAGE_LAYOUT_GENERAL; ib[i].srcQueueFamilyIndex = ib[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        ib[i].image = img_[i]; ib[i].subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}; ib[i].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT; }
    f_->vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 5, ib); f_->vkEndCommandBuffer(cb);
    VkSubmitInfo su{VK_STRUCTURE_TYPE_SUBMIT_INFO}; su.commandBufferCount = 1; su.pCommandBuffers = &cb; VkResult r = f_->vkQueueSubmit(ctx_.queue, 1, &su, nullptr); if (r == VK_SUCCESS) f_->vkQueueWaitIdle(ctx_.queue);
    f_->vkFreeCommandBuffers(d, pool_, 1, &cb); if (r != VK_SUCCESS) return fail("x3 layout transition submit failed");
    havePrev_ = false; cur_ = 0; return true;
}

void FgX3Pass::release() {
    if (!f_) return; VkDevice d = ctx_.device; if (d && f_->vkQueueWaitIdle) { /* callers wait for GPU idle first */ }
    if (dp_) f_->vkDestroyDescriptorPool(d, dp_, nullptr); dp_ = nullptr; ds_[0] = ds_[1] = nullptr;
    for (auto& v : view_) if (v) f_->vkDestroyImageView(d, v, nullptr), v = nullptr;
    if (depthInView_) f_->vkDestroyImageView(d, depthInView_, nullptr); depthInView_ = nullptr; if (mvView_) f_->vkDestroyImageView(d, mvView_, nullptr); mvView_ = nullptr;
    for (auto& i : img_) if (i) f_->vkDestroyImage(d, i, nullptr), i = nullptr;
    for (auto& m : mem_) if (m) f_->vkFreeMemory(d, m, nullptr), m = nullptr;
    for (auto& b : buf_) if (b) f_->vkDestroyBuffer(d, b, nullptr), b = nullptr;
    for (auto& m : bmem_) if (m) f_->vkFreeMemory(d, m, nullptr), m = nullptr;
    bytes_ = 0; havePrev_ = false; depthIn_ = nullptr;
}

void FgX3Pass::destroy() {
    if (!f_) return;
    release();
    VkDevice d = ctx_.device;
    if (qp_) f_->vkDestroyQueryPool(d, qp_, nullptr); qp_ = nullptr;
    if (qpP_) f_->vkDestroyQueryPool(d, qpP_, nullptr); qpP_ = nullptr;
    if (pool_) f_->vkDestroyCommandPool(d, pool_, nullptr); pool_ = nullptr;
    if (pipe_) f_->vkDestroyPipeline(d, pipe_, nullptr); pipe_ = nullptr;
    if (sm_) f_->vkDestroyShaderModule(d, sm_, nullptr); sm_ = nullptr;
    if (pl_) f_->vkDestroyPipelineLayout(d, pl_, nullptr); pl_ = nullptr;
    if (dsl_) f_->vkDestroyDescriptorSetLayout(d, dsl_, nullptr); dsl_ = nullptr;
    if (lin_) f_->vkDestroySampler(d, lin_, nullptr); lin_ = nullptr;
    if (nea_) f_->vkDestroySampler(d, nea_, nullptr); nea_ = nullptr;
    delete f_; f_ = nullptr; ctx_ = {};
    tsOn_ = false; prof_ = false; tsValid_ = false;
}

static void globalBarrier(FgX3Pass::Fns* f, VkCommandBuffer cb) {
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; mb.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    f->vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
}
static void copyImg(FgX3Pass::Fns* f, VkCommandBuffer cb, VkImage src, VkImage dst, uint32_t w, uint32_t h) {
    VkImageCopy c{}; c.srcSubresource = c.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; c.extent = {w, h, 1};
    f->vkCmdCopyImage(cb, src, VK_IMAGE_LAYOUT_GENERAL, dst, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
}

void FgX3Pass::recordHudless(VkCommandBuffer cb, VkImage mcColor) {
    globalBarrier(f_, cb); copyImg(f_, cb, mcColor, img_[cur_], dw_, dh_); globalBarrier(f_, cb);
}

void FgX3Pass::pollTs() {
    uint32_t s = (tsSlot_ + 8 - 4) % 8; if (tsSlotFrame_[s] == 0) return; uint64_t t[2];
    if (f_->vkGetQueryPoolResults(ctx_.device, qp_, s * 2, 2, sizeof t, t, 8, VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) return;
    tsMs_ = (float)((t[1] - t[0]) * tsPeriod_ * 1e-6); tsValid_ = true;
}

bool FgX3Pass::recordGenerateN(VkCommandBuffer cb, VkImage mcFinal, int idx, int n, VkImage dst) { return recordGenerateImpl(cb, mcFinal, idx, idx / (float)n, dst, false, n); }
bool FgX3Pass::recordGenerateAt(VkCommandBuffer cb, VkImage mcFinal, float t, bool copyFinal, VkImage dst, bool tsEnd) { return recordGenerateImpl(cb, mcFinal, copyFinal ? 1 : 3, t, dst, tsEnd); }
bool FgX3Pass::readProfile(float* o) { if (!prof_) return false; uint64_t t[5]; if (f_->vkGetQueryPoolResults(ctx_.device, qpP_, 0, 5, sizeof t, t, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) != VK_SUCCESS) return false; for (int i = 0; i < 4; ++i) o[i] = (float)((t[i + 1] - t[i]) * tsPeriod_ * 1e-6); return true; }
bool FgX3Pass::readTsNow(float* ms) { if (!tsOn_) return false; uint64_t t[2]; if (f_->vkGetQueryPoolResults(ctx_.device, qp_, tsSlot_ * 2, 2, sizeof t, t, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT) != VK_SUCCESS) return false; *ms = (float)((t[1] - t[0]) * tsPeriod_ * 1e-6); return true; }
bool FgX3Pass::recordGenerateImpl(VkCommandBuffer cb, VkImage mcFinal, int idx, float tt, VkImage dst, bool tsEnd, int n) {
    if (!havePrev_ || !pipe_ || !dp_) return false;
    if (idx == 1) {
        copyImg(f_, cb, mcFinal, img_[2], dw_, dh_);
        if (tsOn_) { tsSlot_ = (tsSlot_ + 1) % 8; f_->vkCmdResetQueryPool(cb, qp_, tsSlot_ * 2, 2); tsSlotFrame_[tsSlot_] = ++tsFrame_; }
    }
    globalBarrier(f_, cb);
    if (tsOn_ && idx == 1) f_->vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp_, tsSlot_ * 2);
    f_->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe_); f_->vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl_, 0, 1, &ds_[cur_], 0, nullptr);
    bool pf = prof_ && idx == 1; if (pf) { f_->vkCmdResetQueryPool(cb, qpP_, 0, 5); f_->vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qpP_, 0); }
    for (int mode = 0; mode < 4; ++mode) {
        struct { int32_t dw, dh, rw, rh, mode; float t; int32_t p0, p1; } pc = {(int32_t)dw_, (int32_t)dh_, (int32_t)rw_, (int32_t)rh_, mode, tt, (flipY_ ? 1 : 0) | (variant_ << 1), dbgMode_};
        f_->vkCmdPushConstants(cb, pl_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof pc, &pc); if (!(skip_ & (1u << mode)) && !(i64_ && mode == 2)) f_->vkCmdDispatch(cb, (dw_ + 7) / 8, (dh_ + 7) / 8, 1); if (!(i64_ && mode == 2)) globalBarrier(f_, cb); if (pf) f_->vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qpP_, mode + 1);
    }
    if (tsOn_ && (idx == n - 1 || tsEnd)) { f_->vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp_, tsSlot_ * 2 + 1); pollTs(); }
    copyImg(f_, cb, img_[4], dst, dw_, dh_); globalBarrier(f_, cb);
    return true;
}

void FgX3Pass::recordEndFrame(VkCommandBuffer cb) {
    globalBarrier(f_, cb); copyImg(f_, cb, depthIn_, img_[3], rw_, rh_); globalBarrier(f_, cb);
    cur_ = 1 - cur_; havePrev_ = true;   // what was "current" is now the previous frame's world colour; the next recordHudless overwrites the other image
}

}  // namespace fsr4

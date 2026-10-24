#include <cstdlib>
#include "gpu_bridge.hpp"
#include <dirent.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <algorithm>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cerrno>
#include <set>
#include <random>
#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434
#endif
#ifndef SYS_pidfd_getfd
#define SYS_pidfd_getfd 438
#endif
using namespace fsr4ipc;
namespace fsr4 {

struct GpuBridge::Fns {
#define F(n) PFN_##n n = nullptr;
    F(vkGetDeviceProcAddr) F(vkGetPhysicalDeviceMemoryProperties) F(vkCreateImage) F(vkDestroyImage) F(vkGetImageMemoryRequirements) F(vkAllocateMemory) F(vkFreeMemory)
    F(vkBindImageMemory) F(vkCreateBuffer) F(vkDestroyBuffer) F(vkGetBufferMemoryRequirements) F(vkBindBufferMemory) F(vkMapMemory) F(vkUnmapMemory)
    F(vkCreateCommandPool) F(vkDestroyCommandPool) F(vkAllocateCommandBuffers) F(vkFreeCommandBuffers) F(vkBeginCommandBuffer) F(vkEndCommandBuffer) F(vkCmdPipelineBarrier)
    F(vkCmdCopyBufferToImage) F(vkCmdCopyImage) F(vkQueueSubmit) F(vkQueueWaitIdle) F(vkGetMemoryFdPropertiesKHR) F(vkCmdCopyImageToBuffer)
    F(vkCreateSemaphore) F(vkDestroySemaphore) F(vkImportSemaphoreFdKHR) F(vkSignalSemaphore) F(vkGetSemaphoreCounterValue)
#undef F
};

static VkFormat vkFormatOf(Fmt f) {
    switch (f) { case Fmt::RGBA16F: return VK_FORMAT_R16G16B16A16_SFLOAT; case Fmt::R32F: return VK_FORMAT_R32_SFLOAT; case Fmt::RG16F: return VK_FORMAT_R16G16_SFLOAT; case Fmt::RGBA8: return VK_FORMAT_R8G8B8A8_UNORM; default: return VK_FORMAT_UNDEFINED; }
}

namespace {
struct FdInfo { int fd; uint64_t ino; off_t size; };
thread_local std::string lastFdError;
const char* bridgeResultName(BridgeResult result) {
    switch (result) {
        case BridgeResult::Ok: return "ok";
        case BridgeResult::NotRunning: return "helper not running";
        case BridgeResult::Timeout: return "helper timeout";
        case BridgeResult::HelperDied: return "helper died";
        case BridgeResult::HelperStalled: return "helper stalled";
        case BridgeResult::Rejected: return "request rejected";
        case BridgeResult::Internal: return "invalid IPC response";
    }
    return "unknown bridge error";
}
std::string bridgeError(BridgeResult result, const Response& response) {
    return std::string(bridgeResultName(result)) + (response.message[0] ? ": " + std::string(response.message) : "");
}
// Duplicates every dma-buf fd of process `pid` (pidfd_getfd; allowed for the same uid when ptrace_scope permits).
std::vector<FdInfo> dupDmabufs(pid_t pid) {
    static std::atomic<bool> logged{false};
    std::vector<FdInfo> v; int pfd = (int)syscall(SYS_pidfd_open, pid, 0);
    if (pfd < 0) { lastFdError = std::string("pidfd_open: ") + strerror(errno); if (!logged.exchange(true)) fprintf(stderr, "[fsr4] pidfd_open(%d): %s\n", pid, strerror(errno)); return v; }
    std::string dir = "/proc/" + std::to_string(pid) + "/fd"; DIR* d = opendir(dir.c_str()); if (!d) { close(pfd); return v; }
    std::vector<int> nums; while (dirent* e = readdir(d)) { if (e->d_name[0] == '.') continue; char lk[256] = {}; ssize_t n = readlink((dir + "/" + e->d_name).c_str(), lk, sizeof lk - 1);
        if (n > 0 && std::string(lk, n).find("dmabuf") != std::string::npos) nums.push_back(atoi(e->d_name)); }
    closedir(d); std::sort(nums.begin(), nums.end());
    for (int n : nums) { int mine = (int)syscall(SYS_pidfd_getfd, pfd, n, 0); if (mine < 0) { lastFdError = std::string("pidfd_getfd: ") + strerror(errno); if (!logged.exchange(true)) fprintf(stderr, "[fsr4] pidfd_getfd(%d,%d): %s\n", pid, n, strerror(errno)); continue; } struct stat st{}; fstat(mine, &st); off_t sz = lseek(mine, 0, SEEK_END); v.push_back({mine, (uint64_t)st.st_ino, sz}); }
    close(pfd); return v;
}
// Processes that may own the exported dma-bufs: the Wine helper itself and the user's wineserver(s). The shared D3D12 handle's
// fd is held by wineserver (experiment E3b), so new dma-bufs are found by diffing the table before/after the allocation.
std::vector<pid_t> candidatePids(const BridgeClient* cl) {
    std::vector<pid_t> v = cl->helperLinuxPids(); DIR* d = opendir("/proc");
    while (d) { dirent* e = readdir(d); if (!e) break; pid_t pid = atoi(e->d_name); if (pid <= 1) continue; char path[64]; snprintf(path, sizeof path, "/proc/%d/comm", pid); FILE* f = fopen(path, "r"); if (!f) continue; char c[64] = {}; if (fgets(c, sizeof c, f) && strncmp(c, "wineserver", 10) == 0) { struct stat st{}; snprintf(path, sizeof path, "/proc/%d", pid); if (stat(path, &st) == 0 && st.st_uid == getuid()) v.push_back(pid); } fclose(f); }
    if (d) closedir(d); return v;
}
}  // namespace

bool GpuBridge::init(const VkCtx& ctx, BridgeClient* client, std::string* err) {
    ctx_ = ctx; cl_ = client; delete f_; f_ = new Fns();
    
    // Flatpak alone does not prove that external semaphore fd import is unavailable.
    auto gd = (PFN_vkGetDeviceProcAddr)ctx.gipa(ctx.instance, "vkGetDeviceProcAddr"); f_->vkGetDeviceProcAddr = gd;
#define LD(n) f_->n = (PFN_##n)gd(ctx.device, #n); if (!f_->n) { if (err) *err = "missing Vulkan entry point " #n; return false; }
    LD(vkCreateImage) LD(vkDestroyImage) LD(vkGetImageMemoryRequirements) LD(vkAllocateMemory) LD(vkFreeMemory) LD(vkBindImageMemory) LD(vkCreateBuffer) LD(vkDestroyBuffer)
    LD(vkGetBufferMemoryRequirements) LD(vkBindBufferMemory) LD(vkMapMemory) LD(vkUnmapMemory) LD(vkCreateCommandPool) LD(vkDestroyCommandPool) LD(vkAllocateCommandBuffers)
    LD(vkFreeCommandBuffers) LD(vkBeginCommandBuffer) LD(vkEndCommandBuffer) LD(vkCmdPipelineBarrier) LD(vkCmdCopyBufferToImage) LD(vkCmdCopyImage) LD(vkQueueSubmit) LD(vkQueueWaitIdle)
    LD(vkGetMemoryFdPropertiesKHR) LD(vkCmdCopyImageToBuffer)
#undef LD
    f_->vkGetPhysicalDeviceMemoryProperties = (PFN_vkGetPhysicalDeviceMemoryProperties)ctx.gipa(ctx.instance, "vkGetPhysicalDeviceMemoryProperties");
    VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cpi.queueFamilyIndex = ctx.queueFamily; cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (f_->vkCreateCommandPool(ctx.device, &cpi, nullptr, &pool_) != VK_SUCCESS) { if (err) *err = "vkCreateCommandPool failed"; return false; }
    return true;
}

void GpuBridge::destroyImages() {
    for (int i = 0; i < kPlanes; ++i) {
        if (img_[i]) f_->vkDestroyImage(ctx_.device, img_[i], nullptr); img_[i] = nullptr;
        if (mem_[i]) f_->vkFreeMemory(ctx_.device, mem_[i], nullptr); mem_[i] = nullptr;
        fds_[i] = -1;
    }
    ready_ = false; fgReady_ = false; nPlanes_ = 4;
}

void GpuBridge::release() {
    if (!f_) return;
    if (ctx_.queue) f_->vkQueueWaitIdle(ctx_.queue);
    destroyImages();
    if (sem_) { semDev_ = ctx_.device; destroySem_ = f_->vkDestroySemaphore; }   // kept: the fence lives as long as the helper (destroyShared)
    if (allocatedInHelper_ && cl_ && cl_->running()) { FrameParams p; p.cmd = kCmdFree; Response r{}; cl_->submit(p, 3000, &r); }
    allocatedInHelper_ = false;
    if (pool_) { f_->vkDestroyCommandPool(ctx_.device, pool_, nullptr); pool_ = nullptr; }
    delete f_; f_ = nullptr;
}

static const struct { VkImageUsageFlags u; const char* n; } kUsages[] = {
    {VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, "SRC|DST|SAMPLED|STORAGE|COLOR"},
    {VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, "SRC|DST|SAMPLED|STORAGE"},
    {VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "SRC|DST|SAMPLED"},
    {VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, "SRC|DST"},
};

bool GpuBridge::importAll(uint32_t usageIdx, int first, int last, std::string* err) {
    VkPhysicalDeviceMemoryProperties mp; f_->vkGetPhysicalDeviceMemoryProperties(ctx_.phys, &mp);
    for (int i = first; i < last; ++i) {
        VkExternalMemoryImageCreateInfo emi{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO}; emi.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, &emi}; ic.imageType = VK_IMAGE_TYPE_2D; ic.format = fmt_[i]; ic.extent = {w_[i], h_[i], 1}; ic.mipLevels = 1; ic.arrayLayers = 1;
        ic.samples = VK_SAMPLE_COUNT_1_BIT; ic.tiling = VK_IMAGE_TILING_OPTIMAL; ic.usage = kUsages[usageIdx].u; ic.sharingMode = VK_SHARING_MODE_EXCLUSIVE; ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkResult r = f_->vkCreateImage(ctx_.device, &ic, nullptr, &img_[i]); if (r) { if (err) *err = "vkCreateImage(shared " + std::to_string(i) + ") = " + std::to_string(r); return false; }
        VkMemoryRequirements mr; f_->vkGetImageMemoryRequirements(ctx_.device, img_[i], &mr);
        VkMemoryFdPropertiesKHR fp{VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR}; r = f_->vkGetMemoryFdPropertiesKHR(ctx_.device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, fds_[i], &fp);
        if (r) { if (err) *err = "vkGetMemoryFdPropertiesKHR = " + std::to_string(r); return false; }
        uint32_t bits = mr.memoryTypeBits & fp.memoryTypeBits; int mt = -1;
        for (uint32_t k = 0; k < mp.memoryTypeCount; ++k) if ((bits & (1u << k)) && (mp.memoryTypes[k].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) { mt = (int)k; break; }
        if (mt < 0) { if (err) *err = "no device-local memory type for imported dma-buf"; return false; }
        VkMemoryDedicatedAllocateInfo ded{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO}; ded.image = img_[i];
        int fdcopy = dup(fds_[i]);   // vkAllocateMemory takes ownership of the fd; keep ours for retries with other usages
        VkImportMemoryFdInfoKHR imp{VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR, &ded}; imp.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT; imp.fd = fdcopy;
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &imp}; mai.allocationSize = mr.size; mai.memoryTypeIndex = (uint32_t)mt;
        r = f_->vkAllocateMemory(ctx_.device, &mai, nullptr, &mem_[i]); if (r) { close(fdcopy); if (err) *err = "vkAllocateMemory(import dma-buf " + std::to_string(i) + ") = " + std::to_string(r); return false; }
        if (f_->vkBindImageMemory(ctx_.device, img_[i], mem_[i], 0)) { if (err) *err = "vkBindImageMemory failed"; return false; }
    }
    return true;
}

bool GpuBridge::selfTest(std::string* err) {
    // 1) pseudo-random pattern per plane -> staging buffer -> shared image (GENERAL); 2) helper reads it back through D3D12 and
    // returns the CRC of the logical rows; 3) we compare with the CRC of the bytes we wrote.
    VkPhysicalDeviceMemoryProperties mp; f_->vkGetPhysicalDeviceMemoryProperties(ctx_.phys, &mp);
    std::vector<uint8_t> pat[kPlanes]; uint32_t expect[kPlanes]; size_t total = 0, off[kPlanes];
    for (int i = 0; i < nPlanes_; ++i) { uint32_t bpp = i == 0 ? bytesPerPixel(cf_) : i == 1 ? 4 : i == 2 ? 4 : i == 3 ? bytesPerPixel(of_) : 4; pat[i].resize((size_t)w_[i] * h_[i] * bpp);
        std::mt19937 g(1234 + i); for (size_t k = 0; k + 4 <= pat[i].size(); k += 4) { uint32_t v = g(); if (i == 0 && cf_ == Fmt::RGBA16F) v &= 0x3BFF3BFFu; if (i == 3 && of_ == Fmt::RGBA16F) v &= 0x3BFF3BFFu; if (i == 1) v &= 0x3F7FFFFFu; if (i == 2) v &= 0x3BFF3BFFu; memcpy(&pat[i][k], &v, 4); }
        expect[i] = crc32(pat[i].data(), pat[i].size()); off[i] = total; total += (pat[i].size() + 255) & ~255ull; }
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size = total; bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT; VkBuffer buf; f_->vkCreateBuffer(ctx_.device, &bi, nullptr, &buf);
    VkMemoryRequirements mr; f_->vkGetBufferMemoryRequirements(ctx_.device, buf, &mr); int mt = -1;
    for (uint32_t k = 0; k < mp.memoryTypeCount; ++k) if ((mr.memoryTypeBits & (1u << k)) && (mp.memoryTypes[k].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { mt = (int)k; break; }
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mai.allocationSize = mr.size; mai.memoryTypeIndex = (uint32_t)mt; VkDeviceMemory bm; f_->vkAllocateMemory(ctx_.device, &mai, nullptr, &bm); f_->vkBindBufferMemory(ctx_.device, buf, bm, 0);
    uint8_t* p; f_->vkMapMemory(ctx_.device, bm, 0, total, 0, (void**)&p); for (int i = 0; i < nPlanes_; ++i) memcpy(p + off[i], pat[i].data(), pat[i].size()); f_->vkUnmapMemory(ctx_.device, bm);
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cai.commandPool = pool_; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1; VkCommandBuffer cb; f_->vkAllocateCommandBuffers(ctx_.device, &cai, &cb);
    VkCommandBufferBeginInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; f_->vkBeginCommandBuffer(cb, &cbi);
    for (int i = 0; i < nPlanes_; ++i) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.image = img_[i]; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_GENERAL; b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        f_->vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        VkBufferImageCopy c{}; c.bufferOffset = off[i]; c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; c.imageExtent = {w_[i], h_[i], 1}; f_->vkCmdCopyBufferToImage(cb, buf, img_[i], VK_IMAGE_LAYOUT_GENERAL, 1, &c);
    }
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    f_->vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr); f_->vkEndCommandBuffer(cb);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &cb; f_->vkQueueSubmit(ctx_.queue, 1, &si, nullptr); f_->vkQueueWaitIdle(ctx_.queue);
    f_->vkFreeCommandBuffers(ctx_.device, pool_, 1, &cb); f_->vkDestroyBuffer(ctx_.device, buf, nullptr); f_->vkFreeMemory(ctx_.device, bm, nullptr);
    FrameParams fp; fp.cmd = kCmdSelfTest; Response rs{}; BridgeResult br = cl_->submit(fp, 4000, &rs);
    if (br != BridgeResult::Ok) { if (err) *err = std::string("selftest request failed: ") + bridgeError(br, rs); return false; }
    bool bad = false; for (int i = 0; i < nPlanes_; ++i) if (rs.sharedCrc[i] != expect[i]) bad = true;
    if (bad && std::getenv("FSR4_CRC_DIAG")) {   // diagnostics for the intermittent mismatch: every plane, then the SAME data read again without rewriting it
        for (int i = 0; i < nPlanes_; ++i) fprintf(stderr, "[crc-diag] plane %d %ux%u: helper 0x%08x native 0x%08x %s\n", i, w_[i], h_[i], rs.sharedCrc[i], expect[i], rs.sharedCrc[i] == expect[i] ? "ok" : "MISMATCH");
        Response r2{}; if (cl_->submit(fp, 4000, &r2) == BridgeResult::Ok) for (int i = 0; i < nPlanes_; ++i) fprintf(stderr, "[crc-diag] re-read plane %d: helper 0x%08x %s\n", i, r2.sharedCrc[i], r2.sharedCrc[i] == expect[i] ? "ok (stale/timing)" : "still wrong (persistent)");
    }
    for (int i = 0; i < nPlanes_; ++i) if (rs.sharedCrc[i] != expect[i]) { if (err) { char b[160]; snprintf(b, sizeof b, "selftest CRC mismatch on plane %d (D3D12 saw 0x%08x, native wrote 0x%08x)", i, rs.sharedCrc[i], expect[i]); *err = b; } return false; }
    return true;
}

bool GpuBridge::allocate(uint32_t rw, uint32_t rh, uint32_t ow, uint32_t oh, Fmt cf, Fmt of, std::string* err) {
    if (!f_ || !cl_ || !cl_->running()) { if (err) *err = "bridge not running"; return false; }
    if (ctx_.queue) f_->vkQueueWaitIdle(ctx_.queue);
    destroyImages();
    if (allocatedInHelper_) { FrameParams p; p.cmd = kCmdFree; Response r{}; cl_->submit(p, 3000, &r); allocatedInHelper_ = false; }
    rw_ = rw; rh_ = rh; ow_ = ow; oh_ = oh; cf_ = cf; of_ = of;
    fmt_[0] = vkFormatOf(cf); fmt_[1] = VK_FORMAT_R32_SFLOAT; fmt_[2] = VK_FORMAT_R16G16_SFLOAT; fmt_[3] = vkFormatOf(of);
    w_[0] = w_[1] = w_[2] = rw; h_[0] = h_[1] = h_[2] = rh; w_[3] = ow; h_[3] = oh;
    // snapshot dma-bufs already held by the helper, ask it to allocate, then take the NEW ones in creation (fd) order
    lastFdError.clear();
    std::set<uint64_t> before; pid_t wpid = 0;
    for (pid_t pid : candidatePids(cl_)) { auto v = dupDmabufs(pid); if (!v.empty() || !wpid) { /* prefer the process that has dmabufs */ } for (auto& f : v) { before.insert(f.ino); close(f.fd); } if (!v.empty()) wpid = pid; }
    FrameParams p; p.cmd = kCmdAlloc; p.renderW = rw; p.renderH = rh; p.outW = ow; p.outH = oh; p.colorFmt = (uint32_t)cf; p.depthFmt = (uint32_t)Fmt::R32F; p.mvFmt = (uint32_t)Fmt::RG16F; p.outFmt = (uint32_t)of; Response rs{};
    BridgeResult br = cl_->submit(p, 4000, &rs); if (br != BridgeResult::Ok) { if (err) *err = std::string("helper alloc failed: ") + bridgeError(br, rs); return false; }
    allocatedInHelper_ = true;
    std::vector<FdInfo> fresh;
    for (pid_t pid : candidatePids(cl_)) { for (auto& f : dupDmabufs(pid)) { if (before.count(f.ino)) close(f.fd); else fresh.push_back(f); } }
    // ROOT CAUSE of the intermittent self-test mismatch (session 9, reproduced with scripts/crc-stress.sh): creation order is NOT ascending fd number (Wine reuses closed fd numbers: 5 of 30
    // allocations had planes 0-2 permuted). The dma-buf inode number is assigned at creation and increases monotonically, so it identifies the creation order.
    if (std::getenv("FSR4_CRC_SHUFFLE") && fresh.size() >= 3) std::rotate(fresh.begin(), fresh.begin() + 1, fresh.begin() + 3);   // test hook: simulate fd-number reuse (planes 0-2 permuted in fd order)
    if (!std::getenv("FSR4_CRC_NOSORT")) std::sort(fresh.begin(), fresh.end(), [](const FdInfo& a, const FdInfo& b) { return a.ino < b.ino; });
    size_t need[4]; for (int i = 0; i < 4; ++i) need[i] = (size_t)w_[i] * h_[i] * (i == 0 ? bytesPerPixel(cf) : i == 3 ? bytesPerPixel(of) : 4);
    if (std::getenv("FSR4_CRC_DIAG")) { std::string d; for (auto& f : fresh) d += " [ino=" + std::to_string(f.ino) + " size=" + std::to_string(f.size) + "]"; fprintf(stderr, "[crc-diag] fresh dma-bufs in fd order:%s\n", d.c_str()); }
    if (fresh.size() < 4) { for (auto& f : fresh) close(f.fd); if (err) *err = "expected 4 new dma-buf fds in the Wine helper, found " + std::to_string(fresh.size()) + (lastFdError.empty() ? "" : " (" + lastFdError + ")"); return false; }
    if (fresh.size() > 4) { for (size_t i = 0; i + 4 < fresh.size(); ++i) close(fresh[i].fd); fresh.erase(fresh.begin(), fresh.end() - 4); }
    for (int i = 0; i < 4; ++i) { if ((size_t)fresh[i].size < need[i]) { for (auto& f : fresh) close(f.fd); if (err) *err = "dma-buf " + std::to_string(i) + " smaller than its texture"; return false; } fds_[i] = fresh[i].fd; }
    std::string last;
    for (uint32_t u = 0; u < sizeof kUsages / sizeof kUsages[0]; ++u) {
        for (int i = 0; i < 4; ++i) { if (img_[i]) { f_->vkDestroyImage(ctx_.device, img_[i], nullptr); img_[i] = nullptr; } if (mem_[i]) { f_->vkFreeMemory(ctx_.device, mem_[i], nullptr); mem_[i] = nullptr; } }
        std::string e; if (!importAll(u, 0, 4, &e)) { last = std::string("usage ") + kUsages[u].n + ": " + e; continue; }
        if (!selfTest(&e)) { last = std::string("usage ") + kUsages[u].n + ": " + e; if (!cl_->running()) break; continue; }
        usageName_ = kUsages[u].n; usageIdx_ = u; ready_ = true; nPlanes_ = 4; for (int i = 0; i < 4; ++i) close(fds_[i]);
        return true;
    }
    for (int i = 0; i < 4; ++i) if (fds_[i] >= 0) close(fds_[i]);
    destroyImages(); if (err) *err = "no usage combination produced a verified import (" + last + ")"; return false;
}

void GpuBridge::recordCopyToShared(VkCommandBuffer cb, int plane, VkImage src, VkImageLayout srcLayout) {
    VkImageCopy c{}; c.srcSubresource = c.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; c.extent = {w_[plane], h_[plane], 1};
    f_->vkCmdCopyImage(cb, src, srcLayout, img_[plane], VK_IMAGE_LAYOUT_GENERAL, 1, &c);
}
void GpuBridge::recordCopyFromShared(VkCommandBuffer cb, int plane, VkImage dst, VkImageLayout dstLayout) {
    VkImageCopy c{}; c.srcSubresource = c.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; c.extent = {w_[plane], h_[plane], 1};
    f_->vkCmdCopyImage(cb, img_[plane], VK_IMAGE_LAYOUT_GENERAL, dst, dstLayout, 1, &c);
}

// ---- shared fence -----------------------------------------------------------------------------------------------
bool GpuBridge::signalShared(uint64_t value) {
    if (!sem_ || !f_->vkSignalSemaphore) return false;
    VkSemaphoreSignalInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO}; si.semaphore = sem_; si.value = value; return f_->vkSignalSemaphore(ctx_.device, &si) == VK_SUCCESS;
}

int64_t GpuBridge::sharedCounter() {
    if (!sem_ || !semDev_ || !getCounter_) return -1;
    uint64_t v = 0; return getCounter_(semDev_, sem_, &v) == VK_SUCCESS ? (int64_t)v : -1;
}

bool GpuBridge::initSharedFence(std::string* err) {
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    if (!f_ || !cl_ || !cl_->running()) return fail("bridge not running");
    if (sharedFenceDisabled_) return fail("shared fence disabled by user");
    auto gd = f_->vkGetDeviceProcAddr;
#define LDO(n) f_->n = (PFN_##n)gd(ctx_.device, #n);
    LDO(vkCreateSemaphore) LDO(vkDestroySemaphore) LDO(vkImportSemaphoreFdKHR) LDO(vkSignalSemaphore) LDO(vkGetSemaphoreCounterValue)
#undef LDO
    if (!f_->vkImportSemaphoreFdKHR || !f_->vkSignalSemaphore || !f_->vkGetSemaphoreCounterValue || !f_->vkCreateSemaphore) return fail("device lacks VK_KHR_external_semaphore_fd / timeline semaphores");
    if (sem_ && semDev_ == ctx_.device) return true;   // already imported earlier in this bridge session
    auto syncobjFds = [&] {
        std::set<std::pair<pid_t, int>> s;
        for (pid_t pid : candidatePids(cl_)) { std::string dir = "/proc/" + std::to_string(pid) + "/fd"; DIR* d = opendir(dir.c_str()); if (!d) continue;
            while (dirent* e = readdir(d)) { if (e->d_name[0] == '.') continue; char lk[256] = {}; ssize_t n = readlink((dir + "/" + e->d_name).c_str(), lk, sizeof lk - 1); if (n > 0 && std::string(lk, n).find("syncobj") != std::string::npos) s.insert({pid, atoi(e->d_name)}); }
            closedir(d); }
        return s; };
    auto before = syncobjFds();
    FrameParams p; p.cmd = kCmdFenceInit; Response rs{}; BridgeResult br = cl_->submit(p, 3000, &rs);
    if (br != BridgeResult::Ok) return fail(std::string("helper could not create the shared fence: ") + bridgeError(br, rs));
    auto after = syncobjFds(); std::string last = "no new syncobj fd appeared in the helper";
    unsigned tried = 0;
    for (auto& k : after) {
        if (before.count(k)) continue;
        if (++tried > 8) { last = "too many shared-fence fd candidates"; break; }
        int pfd = (int)syscall(SYS_pidfd_open, k.first, 0); if (pfd < 0) continue; int mine = (int)syscall(SYS_pidfd_getfd, pfd, k.second, 0); close(pfd); if (mine < 0) continue;
        VkSemaphoreTypeCreateInfo tci{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO}; tci.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        VkExportSemaphoreCreateInfo ex{VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO, &tci}; ex.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &ex}; VkSemaphore s = nullptr;
        if (f_->vkCreateSemaphore(ctx_.device, &sci, nullptr, &s) != VK_SUCCESS) { close(mine); last = "vkCreateSemaphore(timeline, opaque fd) failed"; continue; }
        VkImportSemaphoreFdInfoKHR ii{VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR}; ii.semaphore = s; ii.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT; ii.fd = mine;
        VkResult r = f_->vkImportSemaphoreFdKHR(ctx_.device, &ii); if (r != VK_SUCCESS) { close(mine); f_->vkDestroySemaphore(ctx_.device, s, nullptr); last = "vkImportSemaphoreFdKHR = " + std::to_string(r); continue; }
        sem_ = s; semDev_ = ctx_.device; destroySem_ = f_->vkDestroySemaphore; getCounter_ = f_->vkGetSemaphoreCounterValue;
        fenceValue_ = 2;
        if (!signalShared(1)) { last = "vkSignalSemaphore(shared) failed"; f_->vkDestroySemaphore(ctx_.device, sem_, nullptr); sem_ = nullptr; continue; }
        FrameParams t; t.cmd = kCmdFenceTest; t.fenceWait = 1; t.fenceSignal = 2;
        Response testResponse{};
        br = cl_->submit(t, 2000, &testResponse);
        uint64_t cv = 0;
        if (br == BridgeResult::Ok) f_->vkGetSemaphoreCounterValue(ctx_.device, sem_, &cv);
        if (br == BridgeResult::Ok && cv == 2) return true;
        last = br == BridgeResult::Rejected ? std::string(testResponse.message) : "shared fence self test failed (counter " + std::to_string(cv) + ")";
        f_->vkDestroySemaphore(ctx_.device, sem_, nullptr); sem_ = nullptr;
        if (!cl_->running()) return fail(last + "; helper stopped");
    }
    return fail(last);
}


// ---- frame generation planes ---------------------------------------------------------------------------------------------------------------
void GpuBridge::globalBarrier(VkCommandBuffer cb) {
    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; mb.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    f_->vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
}
void GpuBridge::recordFgIn(VkCommandBuffer cb, VkImage src) {
    globalBarrier(cb); VkImageCopy c{}; c.srcSubresource = c.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; c.extent = {w_[4], h_[4], 1};
    f_->vkCmdCopyImage(cb, src, VK_IMAGE_LAYOUT_GENERAL, img_[4], VK_IMAGE_LAYOUT_GENERAL, 1, &c); globalBarrier(cb);
}
void GpuBridge::recordFgHudless(VkCommandBuffer cb, VkImage src) {
    globalBarrier(cb); VkImageCopy c{}; c.srcSubresource = c.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; c.extent = {w_[6], h_[6], 1};
    f_->vkCmdCopyImage(cb, src, VK_IMAGE_LAYOUT_GENERAL, img_[6], VK_IMAGE_LAYOUT_GENERAL, 1, &c); globalBarrier(cb);
}
void GpuBridge::recordFgOut(VkCommandBuffer cb, VkImage dst) {
    globalBarrier(cb); VkImageCopy c{}; c.srcSubresource = c.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; c.extent = {w_[5], h_[5], 1};
    f_->vkCmdCopyImage(cb, img_[5], VK_IMAGE_LAYOUT_GENERAL, dst, VK_IMAGE_LAYOUT_GENERAL, 1, &c); globalBarrier(cb);
}
void GpuBridge::releaseFg(bool tellHelper) {
    if (!f_) return;
    if (ctx_.queue) f_->vkQueueWaitIdle(ctx_.queue);
    for (int i = 4; i < kPlanes; ++i) { if (img_[i]) f_->vkDestroyImage(ctx_.device, img_[i], nullptr); img_[i] = nullptr; if (mem_[i]) f_->vkFreeMemory(ctx_.device, mem_[i], nullptr); mem_[i] = nullptr; fds_[i] = -1; }
    if (tellHelper && fgReady_ && cl_ && cl_->running()) { FrameParams p; p.cmd = kCmdFgFree; Response r{}; cl_->submit(p, 3000, &r); }
    fgReady_ = false; nPlanes_ = 4;
}
bool GpuBridge::allocateFg(uint32_t dispW, uint32_t dispH, const char* provider, uint32_t flags, std::string* err) {
    auto fail = [&](const std::string& m) { if (err) *err = m; return false; };
    if (!ready_ || !f_ || !cl_ || !cl_->running()) return fail("base planes not allocated / bridge not running");
    if (fgReady_ && w_[4] == dispW && h_[4] == dispH) return true;
    releaseFg(true);
    std::set<uint64_t> before; for (pid_t pid : candidatePids(cl_)) for (auto& f : dupDmabufs(pid)) { before.insert(f.ino); close(f.fd); }
    FrameParams p; p.cmd = kCmdFgAlloc; p.renderW = rw_; p.renderH = rh_; p.outW = dispW; p.outH = dispH; p.flags = flags; p.provider = provider ? provider : "4.0.1"; Response rs{};
    BridgeResult br = cl_->submit(p, 4000, &rs); if (br != BridgeResult::Ok) return fail(std::string("helper frame generation alloc failed: ") + bridgeError(br, rs));
    { std::string m = rs.message; fgProvider_ = m.rfind("provider=", 0) == 0 ? m.substr(9) : std::string(); }
    std::vector<FdInfo> fresh; for (pid_t pid : candidatePids(cl_)) for (auto& f : dupDmabufs(pid)) { if (before.count(f.ino)) close(f.fd); else fresh.push_back(f); }
    auto cleanup = [&] { for (auto& f : fresh) close(f.fd); FrameParams fr; fr.cmd = kCmdFgFree; Response r2{}; cl_->submit(fr, 3000, &r2); };
    std::sort(fresh.begin(), fresh.end(), [](const FdInfo& a, const FdInfo& b) { return a.ino < b.ino; });   // creation order (see allocate())
    if (fresh.size() < 3) { size_t n = fresh.size(); cleanup(); return fail("expected 3 new dma-buf fds for frame generation, found " + std::to_string(n)); }
    if (fresh.size() > 3) { for (size_t i = 0; i + 3 < fresh.size(); ++i) close(fresh[i].fd); fresh.erase(fresh.begin(), fresh.end() - 3); }
    for (int k = 0; k < 3; ++k) { int i = 4 + k; fmt_[i] = VK_FORMAT_R8G8B8A8_UNORM; w_[i] = dispW; h_[i] = dispH; if ((size_t)fresh[k].size < (size_t)dispW * dispH * 4) { cleanup(); return fail("frame generation dma-buf smaller than its texture"); } fds_[i] = fresh[k].fd; }
    nPlanes_ = 7; std::string last;
    for (uint32_t u0 = 0; u0 < sizeof kUsages / sizeof kUsages[0]; ++u0) {
        uint32_t u = (usageIdx_ + u0) % (sizeof kUsages / sizeof kUsages[0]);   // the usage that worked for the base planes first
        for (int i = 4; i < 7; ++i) { if (img_[i]) { f_->vkDestroyImage(ctx_.device, img_[i], nullptr); img_[i] = nullptr; } if (mem_[i]) { f_->vkFreeMemory(ctx_.device, mem_[i], nullptr); mem_[i] = nullptr; } }
        std::string e; if (!importAll(u, 4, 7, &e)) { last = std::string("usage ") + kUsages[u].n + ": " + e; continue; }
        if (!selfTest(&e)) { last = std::string("usage ") + kUsages[u].n + ": " + e; if (!cl_->running()) break; continue; }
        for (int i = 4; i < 7; ++i) close(fds_[i]); fgReady_ = true; return true;
    }
    for (int i = 4; i < 7; ++i) if (fds_[i] >= 0) close(fds_[i]);
    releaseFg(false); { FrameParams fr; fr.cmd = kCmdFgFree; Response r2{}; cl_->submit(fr, 3000, &r2); }
    return fail("no usage combination produced a verified frame generation import (" + last + ")");
}


static float halfToFloat(uint16_t h) { uint32_t s = (h & 0x8000u) << 16, e = (h >> 10) & 31, m = h & 0x3ffu, x; if (e == 0) { if (!m) x = s; else { e = 1; while (!(m & 0x400)) { m <<= 1; --e; } m &= 0x3ff; x = s | ((e + 112) << 23) | (m << 13); } } else if (e == 31) x = s | 0x7f800000u | (m << 13); else x = s | ((e + 112) << 23) | (m << 13); float f; memcpy(&f, &x, 4); return f; }
bool GpuBridge::debugDumpPlane(int plane, const char* path, std::string* report) {
    if (!ready_ || plane < 1 || plane > 2) { if (report) *report = "plane not ready"; return false; }
    uint32_t w = w_[plane], h = h_[plane], bpp = 4; size_t bytes = (size_t)w * h * bpp;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size = bytes; bi.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT; VkBuffer buf = nullptr; if (f_->vkCreateBuffer(ctx_.device, &bi, nullptr, &buf)) return false;
    VkMemoryRequirements mr; f_->vkGetBufferMemoryRequirements(ctx_.device, buf, &mr); VkPhysicalDeviceMemoryProperties mp; f_->vkGetPhysicalDeviceMemoryProperties(ctx_.phys, &mp); int mt = -1;
    for (uint32_t k = 0; k < mp.memoryTypeCount; ++k) if ((mr.memoryTypeBits & (1u << k)) && (mp.memoryTypes[k].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) { mt = (int)k; break; }
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mai.allocationSize = mr.size; mai.memoryTypeIndex = (uint32_t)mt; VkDeviceMemory mem = nullptr; f_->vkAllocateMemory(ctx_.device, &mai, nullptr, &mem); f_->vkBindBufferMemory(ctx_.device, buf, mem, 0);
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cai.commandPool = pool_; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1; VkCommandBuffer cb; f_->vkAllocateCommandBuffers(ctx_.device, &cai, &cb);
    VkCommandBufferBeginInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; f_->vkBeginCommandBuffer(cb, &cbi); globalBarrier(cb);
    VkBufferImageCopy c{}; c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; c.imageExtent = {w, h, 1}; f_->vkCmdCopyImageToBuffer(cb, img_[plane], VK_IMAGE_LAYOUT_GENERAL, buf, 1, &c); globalBarrier(cb); f_->vkEndCommandBuffer(cb);
    f_->vkQueueWaitIdle(ctx_.queue);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &cb; f_->vkQueueSubmit(ctx_.queue, 1, &si, nullptr); f_->vkQueueWaitIdle(ctx_.queue);
    uint8_t* p; f_->vkMapMemory(ctx_.device, mem, 0, bytes, 0, (void**)&p);
    double sx = 0, sy = 0, ax = 0, ay = 0, mx = 0, my = 0, mn = 1e30, mxd = -1e30; size_t nz = 0, n = (size_t)w * h;
    for (size_t i = 0; i < n; ++i) {
        if (plane == 2) { float x = halfToFloat(((uint16_t*)p)[i * 2]), y = halfToFloat(((uint16_t*)p)[i * 2 + 1]); sx += x; sy += y; ax += fabsf(x); ay += fabsf(y); mx = fmax(mx, fabsf(x)); my = fmax(my, fabsf(y)); if (x != 0 || y != 0) ++nz; }
        else { float d = ((float*)p)[i]; sx += d; mn = fmin(mn, d); mxd = fmax(mxd, d); if (d != 0) ++nz; }
    }
    char b[400];
    if (plane == 2) snprintf(b, sizeof b, "motion vectors %ux%u: mean (%.3f, %.3f) px, mean|.| (%.3f, %.3f), max|.| (%.2f, %.2f), non-zero %.1f%%", w, h, sx / n, sy / n, ax / n, ay / n, mx, my, 100.0 * nz / n);
    else snprintf(b, sizeof b, "depth %ux%u: mean %.5f min %.5f max %.5f non-zero %.1f%%", w, h, sx / n, mn, mxd, 100.0 * nz / n);
    if (path && path[0]) { FILE* fp = fopen(path, "wb"); if (fp) { fwrite(p, 1, bytes, fp); fclose(fp); } }
    f_->vkUnmapMemory(ctx_.device, mem); f_->vkFreeCommandBuffers(ctx_.device, pool_, 1, &cb); f_->vkDestroyBuffer(ctx_.device, buf, nullptr); f_->vkFreeMemory(ctx_.device, mem, nullptr);
    if (report) *report = b; return true;
}

}  // namespace fsr4

void fsr4::GpuBridge::destroyShared() {
    if (sem_ && destroySem_ && semDev_) destroySem_(semDev_, sem_, nullptr);
    sem_ = nullptr; fenceValue_ = 0; semDev_ = nullptr; destroySem_ = nullptr; getCounter_ = nullptr;
}

void fsr4::GpuBridge::setSharedFenceDisabled(bool disabled) {
    sharedFenceDisabled_ = disabled;
    if (disabled) {
        destroyShared();
    }
}

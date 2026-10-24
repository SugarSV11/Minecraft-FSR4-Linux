// Replays recorded game frames through the REAL FG x3 pass (native/src/fgx3_pass.cpp + shaders/fgx3.comp - the exact in-game code path) offline.
// Usage: fgx3_replay W H CASE_DIR...   each CASE_DIR holds c0.rgba c1.rgba (world colour, bottom-up like the game's targets), d0.f32 d1.f32 (depth planes), mv1.f32 (RG float32, plane
// convention, spanning F0->F1) and t.txt ("t1 t2": the real time position of the two truth frames inside the interval). Writes out_1.rgba / out_2.rgba and prints the GPU time of the pair.
#include "fgx3_pass.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using namespace fsr4;
#define VKCK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { printf("FAIL %s = %d (line %d)\n", #x, r_, __LINE__); return 20; } } while (0)
static uint16_t f2h(float f) { uint32_t x; memcpy(&x, &f, 4); uint32_t s = (x >> 16) & 0x8000; int e = ((x >> 23) & 255) - 127 + 15; uint32_t m = x & 0x7fffff; if (e <= 0) return s; if (e >= 31) return s | 0x7c00; return s | (e << 10) | (m >> 13); }
static VkPhysicalDeviceMemoryProperties mp;
static int mtype(uint32_t bits, VkMemoryPropertyFlags w) { for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & w) == w) return (int)i; return -1; }
static std::vector<uint8_t> readf(const std::string& p) { FILE* f = fopen(p.c_str(), "rb"); if (!f) { printf("missing %s\n", p.c_str()); exit(3); } fseek(f, 0, SEEK_END); size_t n = ftell(f); fseek(f, 0, SEEK_SET); std::vector<uint8_t> v(n); if (fread(v.data(), 1, n, f) != n) exit(3); fclose(f); return v; }
int main(int argc, char** argv) {
    if (argc < 4) return 2; uint32_t W = atoi(argv[1]), H = atoi(argv[2]);
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.apiVersion = VK_API_VERSION_1_3; VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &ai; VkInstance inst; VKCK(vkCreateInstance(&ici, nullptr, &inst));
    uint32_t n = 0; vkEnumeratePhysicalDevices(inst, &n, nullptr); std::vector<VkPhysicalDevice> pds(n); vkEnumeratePhysicalDevices(inst, &n, pds.data()); VkPhysicalDevice pd = 0;
    for (auto p : pds) { VkPhysicalDeviceProperties pr; vkGetPhysicalDeviceProperties(p, &pr); if (pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) pd = p; }
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    float prio = 1; VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qci.queueCount = 1; qci.pQueuePriorities = &prio;
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES}; f12.shaderBufferInt64Atomics = VK_TRUE; VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f12}; f2.features.shaderInt64 = VK_TRUE;
    const bool useI64 = getenv("FGX3_INT64") != nullptr; FgX3Pass::setInt64(useI64);
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; if (useI64) dci.pNext = &f2; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci; VkDevice dev; VKCK(vkCreateDevice(pd, &dci, nullptr, &dev)); VkQueue q; vkGetDeviceQueue(dev, 0, 0, &q);
    VkCtx ctx{inst, pd, dev, q, 0, vkGetInstanceProcAddr};
    auto mkImg = [&](VkFormat f, VkImage* im) { VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ic.imageType = VK_IMAGE_TYPE_2D; ic.format = f; ic.extent = {W, H, 1}; ic.mipLevels = 1; ic.arrayLayers = 1; ic.samples = VK_SAMPLE_COUNT_1_BIT; ic.tiling = VK_IMAGE_TILING_OPTIMAL;
        ic.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; vkCreateImage(dev, &ic, nullptr, im); VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev, *im, &mr);
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mai.allocationSize = mr.size; mai.memoryTypeIndex = mtype(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); VkDeviceMemory m; vkAllocateMemory(dev, &mai, nullptr, &m); vkBindImageMemory(dev, *im, m, 0); };
    VkImage depth, mv, mcColor, mcFinal, dst; mkImg(VK_FORMAT_R32_SFLOAT, &depth); mkImg(VK_FORMAT_R16G16_SFLOAT, &mv); mkImg(VK_FORMAT_R8G8B8A8_UNORM, &mcColor); mkImg(VK_FORMAT_R8G8B8A8_UNORM, &mcFinal); mkImg(VK_FORMAT_R8G8B8A8_UNORM, &dst);
    VkBuffer sb; VkDeviceMemory sm; VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size = (size_t)W * H * 4; bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT; vkCreateBuffer(dev, &bi, nullptr, &sb);
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev, sb, &mr); VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mai.allocationSize = mr.size; mai.memoryTypeIndex = mtype(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT); vkAllocateMemory(dev, &mai, nullptr, &sm); vkBindBufferMemory(dev, sb, sm, 0);
    uint8_t* host; vkMapMemory(dev, sm, 0, bi.size, 0, (void**)&host);
    VkCommandPool pool; VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; vkCreateCommandPool(dev, &cpi, nullptr, &pool);
    auto run = [&](auto&& rec) { VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cai.commandPool = pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1; VkCommandBuffer cb; vkAllocateCommandBuffers(dev, &cai, &cb);
        VkCommandBufferBeginInfo bg{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkBeginCommandBuffer(cb, &bg); rec(cb); vkEndCommandBuffer(cb); VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &cb; vkQueueSubmit(q, 1, &si, nullptr); vkQueueWaitIdle(q); vkFreeCommandBuffers(dev, pool, 1, &cb); };
    auto gbar = [](VkCommandBuffer cb) { VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; mb.srcAccessMask = mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT; vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr); };
    run([&](VkCommandBuffer cb) { for (VkImage im : {depth, mv, mcColor, mcFinal, dst}) { VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.newLayout = VK_IMAGE_LAYOUT_GENERAL; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.image = im; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}; b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT; vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b); } });
    auto upload = [&](VkImage im) { run([&](VkCommandBuffer cb) { VkBufferImageCopy c{}; c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; c.imageExtent = {W, H, 1}; vkCmdCopyBufferToImage(cb, sb, im, VK_IMAGE_LAYOUT_GENERAL, 1, &c); gbar(cb); }); };
    auto download = [&](VkImage im) { run([&](VkCommandBuffer cb) { gbar(cb); VkBufferImageCopy c{}; c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; c.imageExtent = {W, H, 1}; vkCmdCopyImageToBuffer(cb, im, VK_IMAGE_LAYOUT_GENERAL, sb, 1, &c); gbar(cb); }); };
    FgX3Pass pass; std::string err; if (!pass.init(ctx, &err) || !pass.alloc(W, H, W, H, depth, mv, &err)) { printf("x3 init failed: %s\n", err.c_str()); return 3; }
    size_t N = (size_t)W * H;
    for (int a = 3; a < argc; ++a) {
        std::string d = argv[a]; auto c0 = readf(d + "/c0.rgba"), c1 = readf(d + "/c1.rgba"), d0 = readf(d + "/d0.f32"), d1 = readf(d + "/d1.f32"), mvv = readf(d + "/mv1.f32"), tt = readf(d + "/t.txt"); std::vector<float> tl; { std::string s((char*)tt.data(), tt.size()); const char* p = s.c_str(); char* e; for (;;) { float v = strtof(p, &e); if (e == p) break; tl.push_back(v); p = e; } } int M = (int)tl.size(); if (M < 1) { printf("empty t.txt\n"); return 4; } float t1 = tl[0], t2 = tl.back();
        pass.invalidate();
        memcpy(host, c0.data(), N * 4); upload(mcColor); run([&](VkCommandBuffer cb) { pass.recordHudless(cb, mcColor); });
        memcpy(host, d0.data(), N * 4); upload(depth); run([&](VkCommandBuffer cb) { pass.recordEndFrame(cb); });
        memcpy(host, c1.data(), N * 4); upload(mcColor); run([&](VkCommandBuffer cb) { pass.recordHudless(cb, mcColor); });
        memcpy(host, d1.data(), N * 4); upload(depth);
        { const float* m = (const float*)mvv.data(); for (size_t i = 0; i < N * 2; ++i) ((uint16_t*)host)[i] = f2h(m[i]); } upload(mv);
        memcpy(host, c1.data(), N * 4); upload(mcFinal);
        double ms = 0;
        for (int i = 0; i < M; ++i) { bool ok = false; auto t0 = std::chrono::steady_clock::now(); run([&](VkCommandBuffer cb) { ok = pass.recordGenerateAt(cb, mcFinal, tl[i], i == 0, dst, i == M - 1); }); ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); if (!ok) { printf("generate refused\n"); return 4; }
            download(dst); FILE* f = fopen((d + "/out_" + std::to_string(i + 1) + ".rgba").c_str(), "wb"); fwrite(host, 1, N * 4, f); fclose(f); }
        if (getenv("FSR4_GPU_TS")) { std::vector<float> v; for (int r = 0; r < 40; ++r) { run([&](VkCommandBuffer cb) { for (int i = 0; i < M; ++i) pass.recordGenerateAt(cb, mcFinal, tl[i], i == 0, dst, i == M - 1); }); float g; if (pass.readTsNow(&g)) v.push_back(g); }
            float pr[4]; if (pass.readProfile(pr)) printf("PROFILE clear %.4f  splatZ %.4f  recordId %.4f  resolve %.4f ms (first frame)\n", pr[0], pr[1], pr[2], pr[3]);
            if (!v.empty()) { std::sort(v.begin(), v.end()); printf("GPUMS %.4f (median of %zu, both frames)\n", v[v.size() / 2], v.size()); } }
        printf("%s: M=%d t=(%.3f ... %.3f) wall %.2f ms (includes submit/wait)\n", d.c_str(), M, t1, t2, ms);
    }
    vkDeviceWaitIdle(dev); pass.release(); return 0;
}

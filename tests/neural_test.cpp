// Parity + timing test of the Vulkan neural corrector engine (native/src/neural_pass.cpp, shaders/neural_sel.comp) against the PyTorch reference.
// Usage: neural_test WEIGHTS.bin SAMPLE_DIR CHANNELS [reps=50] [tileFraction=1.0] [W H]
//   SAMPLE_DIR from tools/ai/neural/dump_sample.py: x.u8 (22 x 720 x 1280 planar uint8), ref.f32 (3 x 720 x 1280 float, PyTorch output of the deployed model), y.u8 (truth).
// Prints: maximum / mean absolute difference to the reference in 8-bit levels (expected <= 1 level), GPU time per dispatch (timestamps; median of reps) at 1280x720 and extrapolated to 1920x1080.
#include "neural_pass.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using namespace fsr4;
#define VKCK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { printf("FAIL %s = %d (line %d)\n", #x, r_, __LINE__); return 20; } } while (0)
static std::vector<uint8_t> readf(const std::string& p) { FILE* f = fopen(p.c_str(), "rb"); if (!f) { printf("missing %s\n", p.c_str()); exit(3); } fseek(f, 0, SEEK_END); size_t n = ftell(f); fseek(f, 0, SEEK_SET); std::vector<uint8_t> v(n); if (fread(v.data(), 1, n, f) != n) exit(3); fclose(f); return v; }
static VkPhysicalDeviceMemoryProperties mp;
static int mtype(uint32_t bits, VkMemoryPropertyFlags w) { for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & w) == w) return (int)i; return -1; }
int main(int argc, char** argv) {
    if (argc < 4) return 2; /* optional 6th/7th arg: W H of the sample (timing at native 1080p; parity needs the 1280x720 reference) */ uint32_t W = 1280, H = 720; if (argc > 7) { W = atoi(argv[6]); H = atoi(argv[7]); } int C = atoi(argv[3]); int reps = argc > 4 ? atoi(argv[4]) : 50; double frac = argc > 5 ? atof(argv[5]) : 1.0;
    auto wb = readf(argv[1]); auto x = readf(std::string(argv[2]) + "/x.u8"); auto ref = readf(std::string(argv[2]) + "/ref.f32"); size_t N = (size_t)W * H;
    if (x.size() != 22 * N || ref.size() != 3 * N * 4) { printf("sample size mismatch\n"); return 3; }
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.apiVersion = VK_API_VERSION_1_3; VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &ai; VkInstance inst; VKCK(vkCreateInstance(&ici, nullptr, &inst));
    uint32_t n = 0; vkEnumeratePhysicalDevices(inst, &n, nullptr); std::vector<VkPhysicalDevice> pds(n); vkEnumeratePhysicalDevices(inst, &n, pds.data()); VkPhysicalDevice pd = 0;
    for (auto p : pds) { VkPhysicalDeviceProperties pr; vkGetPhysicalDeviceProperties(p, &pr); if (pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) pd = p; }
    vkGetPhysicalDeviceMemoryProperties(pd, &mp); VkPhysicalDeviceProperties dp; vkGetPhysicalDeviceProperties(pd, &dp);
    float prio = 1; VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qci.queueCount = 1; qci.pQueuePriorities = &prio;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci; VkDevice dev; VKCK(vkCreateDevice(pd, &dci, nullptr, &dev)); VkQueue q; vkGetDeviceQueue(dev, 0, 0, &q);
    VkCtx ctx{inst, pd, dev, q, 0, vkGetInstanceProcAddr};
    VkImage img[8]; VkImageView view[8]; VkDeviceMemory mem[8];
    for (int i = 0; i < 8; ++i) {
        VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ic.imageType = VK_IMAGE_TYPE_2D; ic.format = VK_FORMAT_R8G8B8A8_UNORM; ic.extent = {W, H, 1}; ic.mipLevels = 1; ic.arrayLayers = 1; ic.samples = VK_SAMPLE_COUNT_1_BIT; ic.tiling = VK_IMAGE_TILING_OPTIMAL;
        ic.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT; VKCK(vkCreateImage(dev, &ic, nullptr, &img[i]));
        VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev, img[i], &mr); VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mai.allocationSize = mr.size; mai.memoryTypeIndex = mtype(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VKCK(vkAllocateMemory(dev, &mai, nullptr, &mem[i])); VKCK(vkBindImageMemory(dev, img[i], mem[i], 0));
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = img[i]; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = ic.format; vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}; VKCK(vkCreateImageView(dev, &vi, nullptr, &view[i]));
    }
    VkBuffer sb; VkDeviceMemory sm; VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size = N * 4; bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT; vkCreateBuffer(dev, &bi, nullptr, &sb);
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev, sb, &mr); VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mai.allocationSize = mr.size; mai.memoryTypeIndex = mtype(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(dev, &mai, nullptr, &sm); vkBindBufferMemory(dev, sb, sm, 0); uint8_t* host; vkMapMemory(dev, sm, 0, bi.size, 0, (void**)&host);
    VkBuffer tb; VkDeviceMemory tm; VkBufferCreateInfo tbi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; uint32_t tx = (W + 15) / 16, ty = (H + 15) / 16; tbi.size = tx * ty * 4; tbi.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT; vkCreateBuffer(dev, &tbi, nullptr, &tb);
    VkMemoryRequirements tmr; vkGetBufferMemoryRequirements(dev, tb, &tmr); VkMemoryAllocateInfo tmai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; tmai.allocationSize = tmr.size; tmai.memoryTypeIndex = mtype(tmr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkAllocateMemory(dev, &tmai, nullptr, &tm); vkBindBufferMemory(dev, tb, tm, 0); uint32_t* tflags; vkMapMemory(dev, tm, 0, tbi.size, 0, (void**)&tflags);
    VkCommandPool pool; VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; vkCreateCommandPool(dev, &cpi, nullptr, &pool);
    auto run = [&](auto&& rec) { VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cai.commandPool = pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1; VkCommandBuffer cb; vkAllocateCommandBuffers(dev, &cai, &cb);
        VkCommandBufferBeginInfo bg{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkBeginCommandBuffer(cb, &bg); rec(cb); vkEndCommandBuffer(cb); VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &cb; vkQueueSubmit(q, 1, &si, nullptr); vkQueueWaitIdle(q); vkFreeCommandBuffers(dev, pool, 1, &cb); };
    auto gbar = [](VkCommandBuffer cb) { VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; mb.srcAccessMask = mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT; vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr); };
    run([&](VkCommandBuffer cb) { for (int i = 0; i < 8; ++i) { VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.newLayout = VK_IMAGE_LAYOUT_GENERAL; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.image = img[i]; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}; b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT; vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b); } });
    // inputs 0..6 = G,A,B,C0,C1,F,M (3 planar channels each -> rgba8)
    for (int i = 0; i < 7; ++i) { for (size_t p = 0; p < N; ++p) { host[p * 4] = x[(3 * i + 0) * N + p]; host[p * 4 + 1] = x[(3 * i + 1) * N + p]; host[p * 4 + 2] = x[(3 * i + 2) * N + p]; host[p * 4 + 3] = 255; }
        run([&](VkCommandBuffer cb) { VkBufferImageCopy c{}; c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; c.imageExtent = {W, H, 1}; vkCmdCopyBufferToImage(cb, sb, img[i], VK_IMAGE_LAYOUT_GENERAL, 1, &c); gbar(cb); }); }
    float t = x[21 * N] / 255.0f;
    NeuralPass np; std::string err; if (!np.init(ctx, C, (const float*)wb.data(), wb.size() / 4, &err)) { printf("init failed: %s\n", err.c_str()); return 4; }
    for (uint32_t i = 0; i < tx * ty; ++i) tflags[i] = (double)rand() / RAND_MAX < frac ? 1u : 0u;
    np.bind(view[0], view[1], view[2], view[3], view[4], view[5], view[6], view[7], tb, W, H);
    VkQueryPool qp; VkQueryPoolCreateInfo qi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO}; qi.queryType = VK_QUERY_TYPE_TIMESTAMP; qi.queryCount = 2; vkCreateQueryPool(dev, &qi, nullptr, &qp);
    // parity (all tiles)
    run([&](VkCommandBuffer cb) { np.record(cb, t, false); gbar(cb); });
    run([&](VkCommandBuffer cb) { VkBufferImageCopy c{}; c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; c.imageExtent = {W, H, 1}; vkCmdCopyImageToBuffer(cb, img[7], VK_IMAGE_LAYOUT_GENERAL, sb, 1, &c); gbar(cb); });
    double maxd = 0, sum = 0; size_t over1 = 0; const float* rf = (const float*)ref.data();
    for (size_t p = 0; p < N; ++p) for (int c = 0; c < 3; ++c) { double r = std::round(std::min(1.0f, std::max(0.0f, rf[c * N + p])) * 255.0), v = host[p * 4 + c]; double d = std::fabs(r - v); maxd = std::max(maxd, d); sum += d; if (d > 1) ++over1; }
    printf("PARITY (%s, C=%d): max |GPU - PyTorch| = %.0f levels, mean = %.5f levels, pixels off by more than 1 level: %zu of %zu (%.4f %%)\n", dp.deviceName, C, maxd, sum / (3.0 * N), over1, 3 * N, 100.0 * over1 / (3.0 * N));
    // timing
    for (int mode = 0; mode < 2; ++mode) {
        std::vector<double> ms; for (int r = 0; r < reps; ++r) {
            run([&](VkCommandBuffer cb) { vkCmdResetQueryPool(cb, qp, 0, 2); vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp, 0); np.record(cb, t, mode == 1); vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp, 1); });
            uint64_t ts[2]; vkGetQueryPoolResults(dev, qp, 0, 2, sizeof ts, ts, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT); ms.push_back((ts[1] - ts[0]) * dp.limits.timestampPeriod * 1e-6); }
        std::sort(ms.begin(), ms.end()); double med = ms[ms.size() / 2]; printf("%s: median GPU %.3f ms at %ux%u (%.3f ms scaled to 1920x1080 by pixel count), weights %.1f KiB, shader C=%d\n", mode ? "tile-gated (fraction run ~ flagged + neighbours)" : "all tiles", med, W, H, med * 1920.0 * 1080.0 / ((double)W * H), np.weightFloats() * 4 / 1024.0, C);
    }
    vkDeviceWaitIdle(dev); np.release(); return 0;
}

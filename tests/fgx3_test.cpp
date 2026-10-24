// Numerical test of the FG x3 generator pass (own backend) on a synthetic scene with known motion: a smooth texture translating left by 6 px between two real frames
// (depth constant, motion vectors = +6 px), a static HUD block that exists only in the final colour, and a second case with a moving foreground rectangle over a static background
// (disocclusion). The generated frames at t=1/3 and t=2/3 are compared with the analytically rendered truth.
#include "fgx3_pass.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace fsr4;
#define VKCK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { printf("FAIL %s = %d (line %d)\n", #x, r_, __LINE__); return 20; } } while (0)
static uint16_t f2h(float f) { uint32_t x; memcpy(&x, &f, 4); uint32_t s = (x >> 16) & 0x8000; int e = ((x >> 23) & 255) - 127 + 15; uint32_t m = x & 0x7fffff; if (e <= 0) return s; if (e >= 31) return s | 0x7c00; return s | (e << 10) | (m >> 13); }
static const uint32_t W = 320, H = 180;
static float tex(float x, float y) { return 128.f + 90.f * sinf(x * 0.05f) * cosf(y * 0.07f); }   // low-frequency: bilinear sampling error is tiny
static VkPhysicalDeviceMemoryProperties mp;
static int mtype(uint32_t bits, VkMemoryPropertyFlags w) { for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & w) == w) return (int)i; return -1; }

int main() {
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.apiVersion = VK_API_VERSION_1_3; VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &ai; VkInstance inst; VKCK(vkCreateInstance(&ici, nullptr, &inst));
    uint32_t n = 0; vkEnumeratePhysicalDevices(inst, &n, nullptr); std::vector<VkPhysicalDevice> pds(n); vkEnumeratePhysicalDevices(inst, &n, pds.data()); VkPhysicalDevice pd = 0;
    for (auto p : pds) { VkPhysicalDeviceProperties pr; vkGetPhysicalDeviceProperties(p, &pr); if (pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) pd = p; }
    if (!pd) { printf("no discrete GPU\n"); return 2; }
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    float prio = 1; VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qci.queueCount = 1; qci.pQueuePriorities = &prio;
    uint32_t qn = 0; vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, nullptr); std::vector<VkQueueFamilyProperties> qf(qn); vkGetPhysicalDeviceQueueFamilyProperties(pd, &qn, qf.data()); for (uint32_t i = 0; i < qn; ++i) if (qf[i].queueFlags & VK_QUEUE_COMPUTE_BIT && qf[i].queueFlags & VK_QUEUE_TRANSFER_BIT) { qci.queueFamilyIndex = i; break; }
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci; VkDevice dev; VKCK(vkCreateDevice(pd, &dci, nullptr, &dev)); VkQueue q; vkGetDeviceQueue(dev, qci.queueFamilyIndex, 0, &q);
    VkCtx ctx{inst, pd, dev, q, qci.queueFamilyIndex, vkGetInstanceProcAddr};
    auto mkImg = [&](VkFormat f, VkImageUsageFlags u, VkImage* im) { VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ic.imageType = VK_IMAGE_TYPE_2D; ic.format = f; ic.extent = {W, H, 1}; ic.mipLevels = 1; ic.arrayLayers = 1; ic.samples = VK_SAMPLE_COUNT_1_BIT; ic.tiling = VK_IMAGE_TILING_OPTIMAL; ic.usage = u; ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        vkCreateImage(dev, &ic, nullptr, im); VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev, *im, &mr); VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mai.allocationSize = mr.size; mai.memoryTypeIndex = mtype(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); VkDeviceMemory m; vkAllocateMemory(dev, &mai, nullptr, &m); vkBindImageMemory(dev, *im, m, 0); };
    VkImage depth, mv, mcColor, mcFinal, dst; const VkImageUsageFlags U = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    mkImg(VK_FORMAT_R32_SFLOAT, U, &depth); mkImg(VK_FORMAT_R16G16_SFLOAT, U, &mv); mkImg(VK_FORMAT_R8G8B8A8_UNORM, U, &mcColor); mkImg(VK_FORMAT_R8G8B8A8_UNORM, U, &mcFinal); mkImg(VK_FORMAT_R8G8B8A8_UNORM, U, &dst);
    VkBuffer sb; VkDeviceMemory sm; VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size = W * H * 8; bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT; vkCreateBuffer(dev, &bi, nullptr, &sb);
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev, sb, &mr); VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mai.allocationSize = mr.size; mai.memoryTypeIndex = mtype(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT); vkAllocateMemory(dev, &mai, nullptr, &sm); vkBindBufferMemory(dev, sb, sm, 0);
    uint8_t* host; vkMapMemory(dev, sm, 0, bi.size, 0, (void**)&host);
    VkCommandPool pool; VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cpi.queueFamilyIndex = qci.queueFamilyIndex; vkCreateCommandPool(dev, &cpi, nullptr, &pool);
    auto run = [&](auto&& rec) { VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cai.commandPool = pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1; VkCommandBuffer cb; vkAllocateCommandBuffers(dev, &cai, &cb);
        VkCommandBufferBeginInfo bg{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkBeginCommandBuffer(cb, &bg); rec(cb); vkEndCommandBuffer(cb); VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &cb; vkQueueSubmit(q, 1, &si, nullptr); vkQueueWaitIdle(q); vkFreeCommandBuffers(dev, pool, 1, &cb); };
    auto gbar = [](VkCommandBuffer cb) { VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; mb.srcAccessMask = mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT; vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr); };
    run([&](VkCommandBuffer cb) { for (VkImage im : {depth, mv, mcColor, mcFinal, dst}) { VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.newLayout = VK_IMAGE_LAYOUT_GENERAL; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.image = im; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}; b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT; vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b); } });
    auto upload = [&](VkImage im, size_t bytesPerPx) { run([&](VkCommandBuffer cb) { VkBufferImageCopy c{}; c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; c.imageExtent = {W, H, 1}; vkCmdCopyBufferToImage(cb, sb, im, VK_IMAGE_LAYOUT_GENERAL, 1, &c); gbar(cb); }); (void)bytesPerPx; };
    auto download = [&](VkImage im) { run([&](VkCommandBuffer cb) { gbar(cb); VkBufferImageCopy c{}; c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; c.imageExtent = {W, H, 1}; vkCmdCopyImageToBuffer(cb, im, VK_IMAGE_LAYOUT_GENERAL, sb, 1, &c); gbar(cb); }); };
    FgX3Pass pass; std::string err; if (!pass.init(ctx, &err) || !pass.alloc(W, H, W, H, depth, mv, &err)) { printf("x3 init failed: %s\n", err.c_str()); return 3; }
    int bad = 0;
    struct Case { const char* name; bool fg; bool tui = false; } cases[] = {{"translating texture + HUD block", false}, {"moving foreground over static background (disocclusion)", true}, {"translucent dark UI overlay (alpha 0.5) over translating texture", false, true}};
    for (auto& cs : cases) {
        pass.invalidate();
        // truth scene: background = texture shifting left by 6 px per real frame; optional foreground square 40x40 moving right 12 px per frame (nearer: larger reversed-Z depth)
        auto world = [&](float tm, uint32_t x, uint32_t y, float* depthOut, float* mvx) { float bgx = (float)x + 6.f * tm, v = tex(bgx, (float)y); *depthOut = 0.001f; *mvx = 6.f;
            if (cs.fg) { float fx0 = 100.f + 12.f * tm; if (x >= fx0 && x < fx0 + 40 && y >= 60 && y < 100) { v = 220.f - 2.f * (float)(x - (uint32_t)fx0); *depthOut = 0.01f; *mvx = -12.f; } } return v; };
        // frame 0 (previous) then frame 1 (current): same upload path as in the game (world-only colour, depth, MV)
        for (int fr = 0; fr < 2; ++fr) {
            for (uint32_t y = 0; y < H; ++y) for (uint32_t x = 0; x < W; ++x) { float d, m; float v = world((float)fr, x, y, &d, &m); uint8_t c = (uint8_t)fminf(255.f, fmaxf(0.f, v)); memcpy(host + ((size_t)y * W + x) * 4, (uint8_t[]){c, c, c, 255}, 4); }
            upload(mcColor, 4);
            run([&](VkCommandBuffer cb) { pass.recordHudless(cb, mcColor); });
            for (uint32_t y = 0; y < H; ++y) for (uint32_t x = 0; x < W; ++x) { float d, m; world((float)fr, x, y, &d, &m); ((float*)host)[y * W + x] = d; } upload(depth, 4);
            for (uint32_t y = 0; y < H; ++y) for (uint32_t x = 0; x < W; ++x) { float d, m; world((float)fr, x, y, &d, &m); ((uint16_t*)host)[(y * W + x) * 2] = f2h(fr && !getenv("FGX3_TEST_BADMV") ? m : 0.f); ((uint16_t*)host)[(y * W + x) * 2 + 1] = 0; } upload(mv, 4);
            if (fr == 1) {   // final colour = world + a HUD block that exists only here
                for (uint32_t y = 0; y < H; ++y) for (uint32_t x = 0; x < W; ++x) { float d, m; float v = world(1.f, x, y, &d, &m); uint8_t c = (uint8_t)fminf(255.f, fmaxf(0.f, v)); if (cs.tui && x >= 100 && x < 220 && y >= 20 && y < 70) { uint8_t cc = (uint8_t)(0.5f * c); memcpy(host + ((size_t)y * W + x) * 4, (uint8_t[]){cc, cc, cc, 255}, 4); } else if (x >= 8 && x < 72 && y >= 140 && y < 172) { host[((size_t)y * W + x) * 4] = 255; host[((size_t)y * W + x) * 4 + 1] = 0; host[((size_t)y * W + x) * 4 + 2] = 0; host[((size_t)y * W + x) * 4 + 3] = 255; } else memcpy(host + ((size_t)y * W + x) * 4, (uint8_t[]){c, c, c, 255}, 4); }
                upload(mcFinal, 4);
                for (int idx = 1; idx <= 2; ++idx) {
                    bool ok = false; run([&](VkCommandBuffer cb) { ok = pass.recordGenerate(cb, mcFinal, idx, dst); }); if (!ok) { printf("recordGenerate refused\n"); return 4; }
                    download(dst); double se = 0, seBg = 0; size_t cnt = 0, cntBg = 0; int uiBad = 0; float t = idx / 3.f;
                    for (uint32_t y = 10; y < H - 10; ++y) for (uint32_t x = 12; x < W - 12; ++x) {
                        if (x >= 8 && x < 72 && y >= 140 && y < 172) { if (host[((size_t)y * W + x) * 4] != 255 || host[((size_t)y * W + x) * 4 + 1] != 0) ++uiBad; continue; }
                        float d, m; float tr = world(t, x, y, &d, &m), g = host[((size_t)y * W + x) * 4]; if (cs.tui && x >= 100 && x < 220 && y >= 20 && y < 70) tr = 0.5f * (float)(uint8_t)fminf(255.f, fmaxf(0.f, tr)); double e = (tr - g) * (tr - g); se += e; ++cnt; bool fgRegion = cs.fg && x + 30 >= 100 + 12 * t - 8 && x <= 100 + 12 * t + 80 && y >= 50 && y < 110; if (!fgRegion) { seBg += e; ++cntBg; } }
                    double psnr = 10 * log10(255.0 * 255.0 / (se / cnt + 1e-9)), psnrBg = 10 * log10(255.0 * 255.0 / (seBg / cntBg + 1e-9));
                    bool ok2 = psnrBg > 38.0 && psnr > (cs.fg ? 28.0 : cs.tui ? 50.0 : 38.0) && uiBad == 0; if (!ok2) ++bad;
                    printf("%-56s t=%d/3: PSNR all %.1f dB, background %.1f dB, HUD pixels altered %d -> %s\n", cs.name, idx, psnr, psnrBg, uiBad, ok2 ? "OK" : "MISMATCH");
                }
            }
            run([&](VkCommandBuffer cb) { pass.recordEndFrame(cb); });
        }
    }
    vkDeviceWaitIdle(dev); double mib = pass.deviceBytes() / 1048576.0; pass.release(); printf("device memory used by the pass: %.1f MiB at %ux%u\nRESULT: %s\n", mib, W, H, bad ? "FAIL" : "PASS"); return bad ? 1 : 0;
}

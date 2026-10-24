// GPU-path test: a standalone native Vulkan "game" (own VkDevice with the external-memory extensions Minecraft will need)
// feeds color/depth/motion-vector images to the OFFICIAL AMD upscaler running under Wine, with NO CPU copies of image data,
// using imported dma-buf textures. Reports the same integrity/quality checks as ipc_test plus a latency breakdown.
// Usage: gpu_ipc_test <provider> <outdir> [seq] [latency] [resize] [fg] [crash]
#include "gpu_bridge.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <unistd.h>
using namespace fsr4; using namespace fsr4ipc; using Clock = std::chrono::steady_clock;

static uint16_t f2h(float f) { uint32_t x; memcpy(&x, &f, 4); uint32_t s = (x >> 16) & 0x8000; int e = ((x >> 23) & 0xff) - 127 + 15; uint32_t m = x & 0x7fffff; if (e <= 0) return (uint16_t)s; if (e >= 31) return (uint16_t)(s | 0x7c00); return (uint16_t)(s | (e << 10) | (m >> 13)); }
static float h2f(uint16_t h) { uint32_t s = (h & 0x8000) << 16, e = (h >> 10) & 31, m = h & 0x3ff, x; if (e == 0) { if (!m) x = s; else { e = 1; while (!(m & 0x400)) { m <<= 1; e--; } m &= 0x3ff; x = s | ((e + 112) << 23) | (m << 13); } } else if (e == 31) x = s | 0x7f800000 | (m << 13); else x = s | ((e + 112) << 23) | (m << 13); float f; memcpy(&f, &x, 4); return f; }
static void scene(float u, float v, float DH, float* rgb) {
    int c = ((int)floorf(u / 24.f) + (int)floorf(v / 24.f)) & 1; float d = (u + v) / 7.f; d -= floorf(d); float st = d < 0.18f ? 1.f : 0.f;
    float rx = u - 700.f, ry = v - 500.f, r = sqrtf(rx * rx + ry * ry) / 9.f; r -= floorf(r); float ring = r < 0.2f ? 1.f : 0.f; float g = 0.15f + 0.55f * c;
    rgb[0] = fminf(1.f, g + 0.35f * st); rgb[1] = fminf(1.f, g * 0.8f + 0.5f * ring); rgb[2] = fminf(1.f, 0.2f + 0.3f * (1.f - c) + 0.4f * st * ring + 0.3f * (v / DH));
}
static float halton(int i, int b) { float f = 1, r = 0; while (i > 0) { f /= b; r += f * (i % b); i /= b; } return r; }
#define VKCK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { printf("FAIL %s = %d (line %d)\n", #x, r_, __LINE__); exit(20); } } while (0)

static bool g8 = false;   // FSR4_RGBA8=1: color input and output are RGBA8_UNORM like Minecraft's main target
struct Img { VkImage img = 0; VkDeviceMemory mem = 0; };
struct Buf { VkBuffer buf = 0; VkDeviceMemory mem = 0; void* ptr = nullptr; size_t size = 0; };
static VkPhysicalDevice gPd; static VkDevice gDev; static VkQueue gQ; static VkPhysicalDeviceMemoryProperties gMp;
static int memType(uint32_t bits, VkMemoryPropertyFlags w) { for (uint32_t i = 0; i < gMp.memoryTypeCount; ++i) if ((bits & (1u << i)) && (gMp.memoryTypes[i].propertyFlags & w) == w) return (int)i; return -1; }
static Img mkImg(VkFormat f, uint32_t w, uint32_t h) { Img i; VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ic.imageType = VK_IMAGE_TYPE_2D; ic.format = f; ic.extent = {w, h, 1}; ic.mipLevels = 1; ic.arrayLayers = 1; ic.samples = VK_SAMPLE_COUNT_1_BIT; ic.tiling = VK_IMAGE_TILING_OPTIMAL; ic.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VKCK(vkCreateImage(gDev, &ic, nullptr, &i.img)); VkMemoryRequirements mr; vkGetImageMemoryRequirements(gDev, i.img, &mr); VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = mr.size; ai.memoryTypeIndex = memType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VKCK(vkAllocateMemory(gDev, &ai, nullptr, &i.mem)); vkBindImageMemory(gDev, i.img, i.mem, 0); return i; }
static Buf mkBuf(size_t n) { Buf b; b.size = n; VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size = n; bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT; VKCK(vkCreateBuffer(gDev, &bi, nullptr, &b.buf));
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(gDev, b.buf, &mr); VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = mr.size; ai.memoryTypeIndex = memType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VKCK(vkAllocateMemory(gDev, &ai, nullptr, &b.mem)); vkBindBufferMemory(gDev, b.buf, b.mem, 0); vkMapMemory(gDev, b.mem, 0, n, 0, &b.ptr); return b; }

struct Rig {
    uint32_t rw, rh, ow, oh; Img color, depth, mv, out; Buf upColor, upDepth, upMv, rb; VkCommandPool pool; VkCommandBuffer cbIn, cbOut; VkFence fence; VkQueryPool qp;
    GpuBridge gb;
};
static void barrierAll(VkCommandBuffer cb, VkImage im, VkImageLayout o, VkImageLayout n) { VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.image = im; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}; b.oldLayout = o; b.newLayout = n; b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT; vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b); }
static void memBarrier(VkCommandBuffer cb) { VkMemoryBarrier m{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; m.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; m.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT; vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &m, 0, nullptr, 0, nullptr); }
static void submitWait(Rig& r, VkCommandBuffer cb) { VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &cb; VKCK(vkQueueSubmit(gQ, 1, &si, r.fence)); VKCK(vkWaitForFences(gDev, 1, &r.fence, VK_TRUE, 10000000000ull)); vkResetFences(gDev, 1, &r.fence); }

static bool setup(Rig& r, BridgeClient& bc, VkInstance inst, uint32_t fam, uint32_t rw, uint32_t rh, uint32_t ow, uint32_t oh, std::string* err) {
    r.rw = rw; r.rh = rh; r.ow = ow; r.oh = oh;
    r.color = mkImg(g8 ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R16G16B16A16_SFLOAT, rw, rh); r.depth = mkImg(VK_FORMAT_R32_SFLOAT, rw, rh); r.mv = mkImg(VK_FORMAT_R16G16_SFLOAT, rw, rh); r.out = mkImg(g8 ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R16G16B16A16_SFLOAT, ow, oh);
    r.upColor = mkBuf((size_t)rw * rh * (g8 ? 4 : 8)); r.upDepth = mkBuf((size_t)rw * rh * 4); r.upMv = mkBuf((size_t)rw * rh * 4); r.rb = mkBuf((size_t)ow * oh * (g8 ? 4 : 8));
    VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cpi.queueFamilyIndex = fam; cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; VKCK(vkCreateCommandPool(gDev, &cpi, nullptr, &r.pool));
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cai.commandPool = r.pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1; vkAllocateCommandBuffers(gDev, &cai, &r.cbIn); vkAllocateCommandBuffers(gDev, &cai, &r.cbOut);
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; vkCreateFence(gDev, &fi, nullptr, &r.fence);
    VkQueryPoolCreateInfo qpi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO}; qpi.queryType = VK_QUERY_TYPE_TIMESTAMP; qpi.queryCount = 4; vkCreateQueryPool(gDev, &qpi, nullptr, &r.qp);
    VkCtx ctx; ctx.instance = inst; ctx.phys = gPd; ctx.device = gDev; ctx.queue = gQ; ctx.queueFamily = fam; ctx.gipa = vkGetInstanceProcAddr;
    if (!r.gb.init(ctx, &bc, err)) return false;
    if (!r.gb.allocate(rw, rh, ow, oh, g8 ? Fmt::RGBA8 : Fmt::RGBA16F, g8 ? Fmt::RGBA8 : Fmt::RGBA16F, err)) return false;
    // initial layouts
    VkCommandBuffer cb = r.cbIn; VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkBeginCommandBuffer(cb, &bi);
    for (VkImage im : {r.color.img, r.depth.img, r.mv.img, r.out.img}) barrierAll(cb, im, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL); vkEndCommandBuffer(cb); submitWait(r, cb);
    return true;
}
static void teardown(Rig& r) {
    vkDeviceWaitIdle(gDev); r.gb.release(); vkDestroyQueryPool(gDev, r.qp, nullptr); vkDestroyFence(gDev, r.fence, nullptr); vkDestroyCommandPool(gDev, r.pool, nullptr);
    for (Img* i : {&r.color, &r.depth, &r.mv, &r.out}) { vkDestroyImage(gDev, i->img, nullptr); vkFreeMemory(gDev, i->mem, nullptr); }
    for (Buf* b : {&r.upColor, &r.upDepth, &r.upMv, &r.rb}) { vkDestroyBuffer(gDev, b->buf, nullptr); vkFreeMemory(gDev, b->mem, nullptr); }
}
// one full frame; times in ms: tIn (CPU upload+copies to shared, GPU wait), tRt (IPC+D3D12), tOut (copy back + wait)
struct T { double in, rt, out, gpuDispatch, helper; };
static BridgeResult frame(Rig& r, BridgeClient& bc, FrameParams p, bool upload, T* t, Response* rs) {
    auto a = Clock::now();
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkResetCommandBuffer(r.cbIn, 0); vkBeginCommandBuffer(r.cbIn, &bi);
    if (upload) {
        VkBufferImageCopy c{}; c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; c.imageExtent = {r.rw, r.rh, 1};
        vkCmdCopyBufferToImage(r.cbIn, r.upColor.buf, r.color.img, VK_IMAGE_LAYOUT_GENERAL, 1, &c); vkCmdCopyBufferToImage(r.cbIn, r.upDepth.buf, r.depth.img, VK_IMAGE_LAYOUT_GENERAL, 1, &c); vkCmdCopyBufferToImage(r.cbIn, r.upMv.buf, r.mv.img, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
        memBarrier(r.cbIn);
    }
    r.gb.recordCopyToShared(r.cbIn, 0, r.color.img, VK_IMAGE_LAYOUT_GENERAL); r.gb.recordCopyToShared(r.cbIn, 1, r.depth.img, VK_IMAGE_LAYOUT_GENERAL); r.gb.recordCopyToShared(r.cbIn, 2, r.mv.img, VK_IMAGE_LAYOUT_GENERAL);
    memBarrier(r.cbIn); vkEndCommandBuffer(r.cbIn); submitWait(r, r.cbIn);
    auto b = Clock::now();
    p.cmd = kCmdFrame; BridgeResult br = bc.submit(p, 30000, rs); auto c2 = Clock::now();
    if (br != BridgeResult::Ok) return br;
    vkResetCommandBuffer(r.cbOut, 0); vkBeginCommandBuffer(r.cbOut, &bi); r.gb.recordCopyFromShared(r.cbOut, 3, r.out.img, VK_IMAGE_LAYOUT_GENERAL); memBarrier(r.cbOut);
    VkBufferImageCopy rc{}; rc.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; rc.imageExtent = {r.ow, r.oh, 1}; vkCmdCopyImageToBuffer(r.cbOut, r.out.img, VK_IMAGE_LAYOUT_GENERAL, r.rb.buf, 1, &rc); memBarrier(r.cbOut); vkEndCommandBuffer(r.cbOut); submitWait(r, r.cbOut);
    auto d = Clock::now(); auto ms = [](Clock::time_point x, Clock::time_point y) { return std::chrono::duration<double, std::milli>(y - x).count(); };
    if (t) *t = {ms(a, b), ms(b, c2), ms(c2, d), rs->gpuDispatchMs, rs->helperTotalMs}; return BridgeResult::Ok;
}
static void fillScene(Rig& r, float pan, int f, int phases, FrameParams& p) {
    float scale = (float)r.ow / r.rw; float jx = halton(f % phases + 1, 2) - .5f, jy = halton(f % phases + 1, 3) - .5f; uint16_t* c = (uint16_t*)r.upColor.ptr; float* d = (float*)r.upDepth.ptr; uint16_t* m = (uint16_t*)r.upMv.ptr;
    for (uint32_t y = 0; y < r.rh; ++y) for (uint32_t x = 0; x < r.rw; ++x) { float s[3]; scene((x + .5f + jx) * scale + pan, (y + .5f + jy) * scale, (float)r.oh, s); if (g8) { uint8_t* b8 = (uint8_t*)r.upColor.ptr + ((size_t)y * r.rw + x) * 4; for (int k = 0; k < 3; ++k) b8[k] = (uint8_t)(255.f * s[k] + .5f); b8[3] = 255; } else { uint16_t* q = c + ((size_t)y * r.rw + x) * 4; q[0] = f2h(s[0]); q[1] = f2h(s[1]); q[2] = f2h(s[2]); q[3] = f2h(1.f); } d[(size_t)y * r.rw + x] = 0.5f; m[((size_t)y * r.rw + x) * 2] = f2h(3.f / scale); m[((size_t)y * r.rw + x) * 2 + 1] = 0; }
    p.renderW = r.rw; p.renderH = r.rh; p.outW = r.ow; p.outH = r.oh; p.jitterX = jx; p.jitterY = jy; p.frameId = f; p.flags = kAutoExposure | kNonLinearSrgb; p.colorFmt = g8 ? 4 : 1; p.outFmt = g8 ? 4 : 1;
}
static double psnrVsRef(const Rig& r, float pan) {
    const uint16_t* o = (const uint16_t*)r.rb.ptr; double se = 0; size_t n = 0; bool fin = true;
    for (uint32_t y = 16; y < r.oh - 16; ++y) for (uint32_t x = 16; x < r.ow - 16; ++x) { float acc[3] = {0, 0, 0}; for (int sy = 0; sy < 4; ++sy) for (int sx = 0; sx < 4; ++sx) { float c[3]; scene(x + (sx + .5f) / 4 + pan, y + (sy + .5f) / 4, (float)r.oh, c); for (int k = 0; k < 3; ++k) acc[k] += c[k] / 16; }
        for (int k = 0; k < 3; ++k) { float v = g8 ? ((const uint8_t*)r.rb.ptr)[((size_t)y * r.ow + x) * 4 + k] / 255.f : h2f(o[((size_t)y * r.ow + x) * 4 + k]); if (!std::isfinite(v)) { fin = false; continue; } double dd = fminf(fmaxf(v, 0.f), 1.f) - acc[k]; se += dd * dd; ++n; } }
    return fin ? 10 * log10(1.0 / (se / n + 1e-12)) : -1;
}
static int fails = 0;
#define EXPECT(c, ...) do { if (!(c)) { ++fails; printf("  FAIL: " __VA_ARGS__); printf("\n"); } else { printf("  ok:   " __VA_ARGS__); printf("\n"); } } while (0)
static double med(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; } static double p95(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() * 95 / 100]; }

int main(int argc, char** argv) {
    g8 = getenv("FSR4_RGBA8") != nullptr; if (argc < 3) return 64; std::string provider = argv[1], outdir = argv[2]; std::vector<std::string> tests; for (int i = 3; i < argc; ++i) tests.push_back(argv[i]); if (tests.empty()) tests = {"seq", "latency", "resize"};
    auto want = [&](const char* t) { return std::find(tests.begin(), tests.end(), t) != tests.end(); };
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.apiVersion = VK_API_VERSION_1_3; VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &ai; VkInstance inst; VKCK(vkCreateInstance(&ici, nullptr, &inst));
    uint32_t n = 0; vkEnumeratePhysicalDevices(inst, &n, nullptr); std::vector<VkPhysicalDevice> pds(n); vkEnumeratePhysicalDevices(inst, &n, pds.data());
    for (auto p : pds) { VkPhysicalDeviceProperties pr; vkGetPhysicalDeviceProperties(p, &pr); if (pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { gPd = p; printf("native Vulkan device: %s (driver %u)\n", pr.deviceName, pr.driverVersion); } }
    vkGetPhysicalDeviceMemoryProperties(gPd, &gMp); float prio = 1; VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qci.queueFamilyIndex = 0; qci.queueCount = 1; qci.pQueuePriorities = &prio;
    const char* ex[] = {"VK_KHR_external_memory", "VK_KHR_external_memory_fd", "VK_EXT_external_memory_dma_buf"};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci; dci.enabledExtensionCount = 3; dci.ppEnabledExtensionNames = ex; VKCK(vkCreateDevice(gPd, &dci, nullptr, &gDev)); vkGetDeviceQueue(gDev, 0, 0, &gQ);
    BridgeConfig cfg; { const char* h = getenv("HOME"); std::string H = h; cfg.provider = provider; cfg.protonDir = H + "/.steam/steam/compatibilitytools.d/GE-Proton11-6-x86_64"; cfg.prefixDir = H + "/.cache/fsr4-linux/pfx_ge"; cfg.steamDir = H + "/.local/share/Steam";
        char cwd[1024]; getcwd(cwd, sizeof cwd); cfg.helperExe = std::string(cwd) + "/build/probe/fsr4_bridge.exe"; cfg.logFile = cfg.helperExe + ".log"; cfg.transferMode = 2; }
    BridgeClient bc; auto t0 = Clock::now(); if (!bc.start(cfg)) { printf("START FAILED: %s\n", bc.error().c_str()); return 2; }
    printf("helper up in %.0f ms\n", std::chrono::duration<double, std::milli>(Clock::now() - t0).count());
    Rig r; std::string err; t0 = Clock::now();
    bool ok = setup(r, bc, inst, 0, 1280, 720, 1920, 1080, &err); EXPECT(ok, "shared textures allocated, imported and CRC-verified in %.0f ms%s%s (usage: %s)", std::chrono::duration<double, std::milli>(Clock::now() - t0).count(), ok ? "" : " - ", err.c_str(), ok ? r.gb.usageUsed() : "-"); if (!ok) { bc.stop(); return 3; }
    FrameParams p; Response rs; T tm; BridgeResult br;
    fillScene(r, 0, 0, 18, p); p.flags |= kReset; br = frame(r, bc, p, true, &tm, &rs); EXPECT(br == BridgeResult::Ok, "warm-up frame (status %u %s)", rs.status, rs.message);
    EXPECT(bc.activeProvider() == provider, "provider reported by AMD runtime == requested ('%s')", bc.activeProvider().c_str());
    if (want("seq")) {
        printf("[seq] 64-frame pan on the GPU path\n"); int bad = 0; std::vector<double> in, rt, out, gp;
        for (int f = 0; f < 64; ++f) { fillScene(r, 3.f * f, f, 18, p); if (f == 0) p.flags |= kReset; br = frame(r, bc, p, true, &tm, &rs); if (br != BridgeResult::Ok) { ++bad; printf("  frame %d %s\n", f, rs.message); continue; }
            if (f >= 4) { in.push_back(tm.in); rt.push_back(tm.rt); out.push_back(tm.out); gp.push_back(tm.gpuDispatch); }
            if (f == 15 || f == 63) { double ps = psnrVsRef(r, 3.f * f); printf("  frame %d PSNR vs reference %.2f dB\\n" + 0, f, ps); if (ps < 15) ++bad; if (f == 63) { std::ofstream o(outdir + "/gpu_seq_" + provider + "_f63.ppm", std::ios::binary); o << "P6\n" << r.ow << " " << r.oh << "\n255\n"; const uint16_t* q = (const uint16_t*)r.rb.ptr; for (size_t i = 0; i < (size_t)r.ow * r.oh; ++i) for (int k = 0; k < 3; ++k) o.put(g8 ? (char)((const uint8_t*)r.rb.ptr)[i * 4 + k] : (char)(int)(255.f * fminf(fmaxf(h2f(q[i * 4 + k]), 0.f), 1.f) + .5f)); } } }
        EXPECT(bad == 0, "64/64 frames valid on the GPU path"); printf("  medians ms: copy-in+wait %.2f | helper round-trip %.2f | copy-out+readback %.2f | GPU dispatch %.3f\n", med(in), med(rt), med(out), med(gp));
    }
    if (want("latency")) {
        printf("[latency] 200 frames, scene not regenerated (isolates the bridge)\n"); std::vector<double> in, rt, out, gp, hp, tot; fillScene(r, 0, 0, 18, p);
        for (int f = 0; f < 220; ++f) { p.frameId = f; p.jitterX = halton(f % 18 + 1, 2) - .5f; p.jitterY = halton(f % 18 + 1, 3) - .5f; auto a = Clock::now(); br = frame(r, bc, p, f == 0, &tm, &rs); if (br != BridgeResult::Ok) { printf("  fail %s\n", rs.message); break; }
            if (f >= 20) { in.push_back(tm.in); rt.push_back(tm.rt); out.push_back(tm.out); gp.push_back(tm.gpuDispatch); hp.push_back(tm.helper); tot.push_back(tm.in + tm.rt + tm.out); } (void)a; }
        printf("  median ms: GPU copy-in+wait %.2f | IPC+D3D12 round-trip %.2f (helper reports %.2f, GPU dispatch %.3f) | copy-out+readback %.2f\n", med(in), med(rt), med(hp), med(gp), med(out));
        printf("  total median %.2f ms, p95 %.2f ms -> bridge-only ceiling ~%.0f fps if fully serialised\n", med(tot), p95(tot), 1000.0 / med(tot));
    }
    if (want("fg")) {
        printf("[fg] frame generation 4.0.1 plumbing: shared planes 4/5 (display RGBA8) imported + CRC-verified, reset frame must reproduce its input, later frames must differ\n");
        std::string e; bool okfg = r.gb.allocateFg(r.ow, r.oh, "4.0.1", kDepthInverted, &e); EXPECT(okfg, "FG context + shared planes allocated, imported and CRC-verified%s%s", okfg ? "" : " - ", e.c_str());
        if (okfg) {
            Img fin = mkImg(VK_FORMAT_R8G8B8A8_UNORM, r.ow, r.oh), gen = mkImg(VK_FORMAT_R8G8B8A8_UNORM, r.ow, r.oh); Buf up = mkBuf((size_t)r.ow * r.oh * 4), rbg = mkBuf((size_t)r.ow * r.oh * 4);
            { VkCommandBuffer cb = r.cbIn; VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkResetCommandBuffer(cb, 0); vkBeginCommandBuffer(cb, &bi);
              for (VkImage im : {fin.img, gen.img}) barrierAll(cb, im, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL); vkEndCommandBuffer(cb); submitWait(r, cb); }
            int bad = 0; double diffReset = -1, diffLater = 0; std::vector<double> fgms; uint32_t pitchW = r.ow;
            for (int f = 0; f < 10; ++f) {
                fillScene(r, 3.f * f, f, 18, p); if (f == 0) p.flags |= kReset; br = frame(r, bc, p, true, &tm, &rs); if (br != BridgeResult::Ok) { ++bad; continue; }
                uint8_t* u = (uint8_t*)up.ptr; for (uint32_t y = 0; y < r.oh; ++y) for (uint32_t x = 0; x < pitchW; ++x) { float sc[3]; scene((float)x + 3.f * f, (float)y, (float)r.oh, sc); uint8_t* q = u + ((size_t)y * pitchW + x) * 4; for (int k = 0; k < 3; ++k) q[k] = (uint8_t)(fminf(fmaxf(sc[k], 0.f), 1.f) * 255.f + .5f); q[3] = 255; }
                VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkResetCommandBuffer(r.cbIn, 0); vkBeginCommandBuffer(r.cbIn, &bi);
                VkBufferImageCopy c{}; c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; c.imageExtent = {r.ow, r.oh, 1}; vkCmdCopyBufferToImage(r.cbIn, up.buf, fin.img, VK_IMAGE_LAYOUT_GENERAL, 1, &c); memBarrier(r.cbIn);
                r.gb.recordFgIn(r.cbIn, fin.img); r.gb.recordFgHudless(r.cbIn, fin.img); vkEndCommandBuffer(r.cbIn); submitWait(r, r.cbIn);
                FrameParams fp; fp.cmd = kCmdFgFrame; fp.frameId = (uint64_t)f + 1; fp.flags = (f == 0 ? kReset : 0) | kDepthInverted; fp.jitterX = p.jitterX; fp.jitterY = p.jitterY; fp.mvScaleX = 1; fp.mvScaleY = 1; fp.frameTimeMs = 16.6f; fp.cameraNear = 0.1f; fp.cameraFar = 1000.f; fp.fovY = 1.f;
                fp.fgCam[4] = 1; fp.fgCam[6] = 1; fp.fgCam[11] = 1; Response fr; br = bc.submit(fp, 30000, &fr); if (br != BridgeResult::Ok) { ++bad; printf("  frame %d FG failed: %s\n", f, fr.message); continue; }
                fgms.push_back(fr.gpuDispatchMs);
                vkResetCommandBuffer(r.cbOut, 0); vkBeginCommandBuffer(r.cbOut, &bi); r.gb.recordFgOut(r.cbOut, gen.img); memBarrier(r.cbOut);
                vkCmdCopyImageToBuffer(r.cbOut, gen.img, VK_IMAGE_LAYOUT_GENERAL, rbg.buf, 1, &c); memBarrier(r.cbOut); vkEndCommandBuffer(r.cbOut); submitWait(r, r.cbOut);
                double sd = 0; const uint8_t* g2 = (const uint8_t*)rbg.ptr; size_t n = (size_t)r.ow * r.oh; for (size_t i = 0; i < n; ++i) for (int k = 0; k < 3; ++k) sd += std::abs((int)g2[i * 4 + k] - (int)u[i * 4 + k]); sd /= (double)(n * 3);
                if (f == 0) diffReset = sd; if (f >= 3) diffLater += sd / 7;
            }
            printf("  mean abs diff generated vs input: reset frame %.3f, later frames avg %.3f (8-bit levels); FG GPU time (helper timestamps, lagging one frame) median %.3f ms\n", diffReset, diffLater, fgms.size() > 3 ? med(std::vector<double>(fgms.begin() + 2, fgms.end())) : -1.0);
            EXPECT(bad == 0, "10/10 FG frames processed"); EXPECT(diffReset >= 0 && diffReset < 1.0, "reset frame reproduces its input through the shared planes (diff %.3f)", diffReset); EXPECT(diffLater > 0.5, "later frames are interpolated (differ from the input, diff %.3f)", diffLater);
            r.gb.releaseFg(true);
            vkDestroyImage(gDev, fin.img, nullptr); vkFreeMemory(gDev, fin.mem, nullptr); vkDestroyImage(gDev, gen.img, nullptr); vkFreeMemory(gDev, gen.mem, nullptr);
        }
    }
    if (want("resize")) {
        printf("[resize] reallocate shared textures at 960x540 -> 1920x1080 output, then back to 1280x720\n"); vkDeviceWaitIdle(gDev); teardown(r); Rig r2;
        uint32_t dims[3][2] = {{960, 540}, {1280, 720}, {1600, 900}}; bool allok = true;
        for (auto& d : dims) { std::string e; auto a = Clock::now(); bool ok2 = setup(r2, bc, inst, 0, d[0], d[1], 1920, 1080, &e); double ms = std::chrono::duration<double, std::milli>(Clock::now() - a).count();
            if (!ok2) { allok = false; printf("  %ux%u setup failed: %s\n", d[0], d[1], e.c_str()); break; }
            fillScene(r2, 0, 0, 18, p); p.flags |= kReset; for (int f = 0; f < 4; ++f) { fillScene(r2, 3.f * f, f, 18, p); if (f == 0) p.flags |= kReset; br = frame(r2, bc, p, true, &tm, &rs); if (br != BridgeResult::Ok) { allok = false; printf("  %ux%u frame fail %s\n", d[0], d[1], rs.message); break; } }
            double ps = psnrVsRef(r2, 9.f); printf("  render %ux%u: re-allocation+verification %.0f ms, 4 frames ok, PSNR %.2f dB\n", d[0], d[1], ms, ps); if (ps < 10) allok = false; teardown(r2); r2 = Rig(); }
        EXPECT(allok, "resolution changes via re-import"); bc.stop(); printf("RESULT: %s (%d failed checks)\n", fails ? "FAIL" : "PASS", fails); return fails ? 1 : 0;
    }
    teardown(r); bc.stop(); printf("RESULT: %s (%d failed checks)\n", fails ? "FAIL" : "PASS", fails); return fails ? 1 : 0;
}

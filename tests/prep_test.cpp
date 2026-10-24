// Numerical test of the motion-vector/depth prep compute pass against a double-precision CPU reference.
// Uses the same shared-texture plumbing as the real bridge (so it also exercises storage-image writes into imported dma-bufs).
#include "prep_pass.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <unistd.h>
using namespace fsr4;
#define VKCK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { printf("FAIL %s = %d (line %d)\n", #x, r_, __LINE__); return 20; } } while (0)
struct M4 { double m[16]; };  // column-major
static M4 mul(const M4& a, const M4& b) { M4 r; for (int c = 0; c < 4; ++c) for (int rr = 0; rr < 4; ++rr) { double s = 0; for (int k = 0; k < 4; ++k) s += a.m[k * 4 + rr] * b.m[c * 4 + k]; r.m[c * 4 + rr] = s; } return r; }
// GL-style (Y up) reversed-Z perspective like Minecraft's; shaders flip Y on output
static M4 persp(double fovy, double aspect, double zn, double zf) { M4 p{}; double f = 1.0 / tan(fovy / 2); p.m[0] = f / aspect; p.m[5] = f; p.m[10] = zn / (zf - zn); p.m[11] = -1; p.m[14] = zf * zn / (zf - zn); /* reversed: ndcz = zn*(zf+z)/(...)*/ return p; }
static M4 inv(const M4& a) { // gauss-jordan
    double A[4][8]; for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) { A[r][c] = a.m[c * 4 + r]; A[r][4 + c] = r == c; }
    for (int i = 0; i < 4; ++i) { int p = i; for (int r = i + 1; r < 4; ++r) if (fabs(A[r][i]) > fabs(A[p][i])) p = r; for (int c = 0; c < 8; ++c) std::swap(A[i][c], A[p][c]); double d = A[i][i]; for (int c = 0; c < 8; ++c) A[i][c] /= d;
        for (int r = 0; r < 4; ++r) if (r != i) { double f = A[r][i]; for (int c = 0; c < 8; ++c) A[r][c] -= f * A[i][c]; } }
    M4 o; for (int r = 0; r < 4; ++r) for (int c = 0; c < 4; ++c) o.m[c * 4 + r] = A[r][4 + c]; return o; }
static void xf(const M4& m, const double* v, double* o) { for (int r = 0; r < 4; ++r) o[r] = m.m[r] * v[0] + m.m[4 + r] * v[1] + m.m[8 + r] * v[2] + m.m[12 + r] * v[3]; }
static float h2f(uint16_t h) { uint32_t s = (h & 0x8000) << 16, e = (h >> 10) & 31, m = h & 0x3ff, x; if (e == 0) { if (!m) x = s; else { e = 1; while (!(m & 0x400)) { m <<= 1; e--; } m &= 0x3ff; x = s | ((e + 112) << 23) | (m << 13); } } else if (e == 31) x = s | 0x7f800000 | (m << 13); else x = s | ((e + 112) << 23) | (m << 13); float f; memcpy(&f, &x, 4); return f; }

int main() {
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.apiVersion = VK_API_VERSION_1_3; VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &ai; VkInstance inst; VKCK(vkCreateInstance(&ici, nullptr, &inst));
    uint32_t n = 0; vkEnumeratePhysicalDevices(inst, &n, nullptr); std::vector<VkPhysicalDevice> pds(n); vkEnumeratePhysicalDevices(inst, &n, pds.data()); VkPhysicalDevice pd = 0;
    for (auto p : pds) { VkPhysicalDeviceProperties pr; vkGetPhysicalDeviceProperties(p, &pr); if (pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) pd = p; }
    float prio = 1; VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qci.queueCount = 1; qci.pQueuePriorities = &prio; const char* ex[] = {"VK_KHR_external_memory", "VK_KHR_external_memory_fd", "VK_EXT_external_memory_dma_buf"};
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES}; f12.timelineSemaphore = VK_TRUE;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &f12}; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci; dci.enabledExtensionCount = 3; dci.ppEnabledExtensionNames = ex; VkDevice dev; VKCK(vkCreateDevice(pd, &dci, nullptr, &dev)); VkQueue q; vkGetDeviceQueue(dev, 0, 0, &q);
    BridgeConfig cfg; std::string H = getenv("HOME"); cfg.provider = "4.1.1"; cfg.protonDir = H + "/.steam/steam/compatibilitytools.d/GE-Proton11-6-x86_64"; cfg.prefixDir = H + "/.cache/fsr4-linux/pfx_ge"; cfg.steamDir = H + "/.local/share/Steam";
    char cwd[512]; getcwd(cwd, sizeof cwd); cfg.helperExe = std::string(cwd) + "/build/probe/fsr4_bridge.exe"; cfg.logFile = cfg.helperExe + ".log"; cfg.transferMode = 2;
    BridgeClient bc; if (!bc.start(cfg)) { printf("start failed: %s\n", bc.error().c_str()); return 2; }
    VkCtx ctx{inst, pd, dev, q, 0, vkGetInstanceProcAddr}; GpuBridge gb; std::string err; const uint32_t W = 640, Hh = 360;
    if (!gb.init(ctx, &bc, &err) || !gb.allocate(W, Hh, 1280, 720, fsr4ipc::Fmt::RGBA8, fsr4ipc::Fmt::RGBA8, &err)) { printf("gb failed: %s\n", err.c_str()); return 3; }
    // fake Minecraft depth image D32 (SAMPLED|TRANSFER_DST), constant depth value & fake color image
    auto mkImg = [&](VkFormat f, VkImageUsageFlags u, VkImage* im, VkDeviceMemory* mem, uint32_t w, uint32_t h) { VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ic.imageType = VK_IMAGE_TYPE_2D; ic.format = f; ic.extent = {w, h, 1}; ic.mipLevels = 1; ic.arrayLayers = 1; ic.samples = VK_SAMPLE_COUNT_1_BIT; ic.tiling = VK_IMAGE_TILING_OPTIMAL; ic.usage = u; ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; vkCreateImage(dev, &ic, nullptr, im);
        VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev, *im, &mr); VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd, &mp); int mt = -1; for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) if ((mr.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) { mt = i; break; }
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mai.allocationSize = mr.size; mai.memoryTypeIndex = mt; vkAllocateMemory(dev, &mai, nullptr, mem); vkBindImageMemory(dev, *im, *mem, 0); };
    VkImage depth, color; VkDeviceMemory dm, cm; mkImg(VK_FORMAT_D32_SFLOAT, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, &depth, &dm, W, Hh); mkImg(VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, &color, &cm, W, Hh);
    PrepPass pp; if (!pp.init(ctx, &err) || !pp.bind(&gb, depth, &err)) { printf("prep init failed: %s\n", err.c_str()); return 4; }
    // matrices
    const double fov = 70.0 * M_PI / 180, aspect = (double)W / Hh, zn = 0.05, zf = 1000; M4 P = persp(fov, aspect, zn, zf); M4 V{}; V.m[0] = V.m[5] = V.m[10] = V.m[15] = 1;   // identity view rotation
    // reversed-Z depth of a point at view distance z (negative z forward): ndcz = P.m[10]*z + P.m[14] / (-z) ... compute via transform
    struct Case { double dist; double dx, dy, dz; const char* name; int ent = 0; } cases[] = {{5.0, 0, 0, 0, "entity: translate+yaw, static camera", 1}, {5.0, 0.5, 0, 0.2, "entity + camera motion", 1}, {5.0, 0, 0, 0, "entity box misses plane (z)", 2}, {5.0, 1.0, 0, 0, "lateral 1 block @5"}, {20.0, 1.0, 0, 0, "lateral 1 block @20"}, {5.0, 0, 0, 0.5, "forward 0.5 block @5"}, {5.0, 0, 0, 0, "static"}};
    int bad = 0;
    for (auto& cs : cases) {
        double pt[4] = {0, 0, -cs.dist, 1}, clip[4]; xf(mul(P, V), pt, clip); float d = (float)(clip[2] / clip[3]);
        // upload constant depth via staging buffer
        VkBuffer sb; VkDeviceMemory sm; VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bi.size = (size_t)W * Hh * 4; bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT; vkCreateBuffer(dev, &bi, nullptr, &sb);
        VkMemoryRequirements mr; vkGetBufferMemoryRequirements(dev, sb, &mr); VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd, &mp); int mt = -1; for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) if ((mr.memoryTypeBits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & 6) == 6) { mt = i; break; }
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; mai.allocationSize = mr.size; mai.memoryTypeIndex = mt; vkAllocateMemory(dev, &mai, nullptr, &sm); vkBindBufferMemory(dev, sb, sm, 0); float* p; vkMapMemory(dev, sm, 0, bi.size, 0, (void**)&p); for (size_t i = 0; i < (size_t)W * Hh; ++i) p[i] = d;
        // previous camera = current camera - camDelta... define: cur camera at origin; prev camera at origin - delta, so camDelta = cur - prev = (dx,dy,dz)
        double delta[3] = {cs.dx, cs.dy, cs.dz}; M4 VP = mul(P, V); M4 invVP = inv(VP); float mats[48]; for (int i = 0; i < 16; ++i) { mats[i] = (float)invVP.m[i]; mats[16 + i] = (float)VP.m[i]; mats[32 + i] = (float)VP.m[i]; } float cd[3] = {(float)delta[0], (float)delta[1], (float)delta[2]};
        VkCommandPool pool; VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; vkCreateCommandPool(dev, &cpi, nullptr, &pool); VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cai.commandPool = pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1; VkCommandBuffer cb; vkAllocateCommandBuffers(dev, &cai, &cb);
        VkCommandBufferBeginInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkBeginCommandBuffer(cb, &cbi);
        for (VkImage im : {depth, color}) { VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.image = im; b.subresourceRange = {im == depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}; b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_GENERAL; b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b); }
        VkBufferImageCopy c{}; c.imageSubresource = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 0, 1}; c.imageExtent = {W, Hh, 1}; vkCmdCopyBufferToImage(cb, sb, depth, VK_IMAGE_LAYOUT_GENERAL, 1, &c);
        // entity: box centred on the depth plane (view-space z=-dist), 1 block half extents; rigid motion = translation (0.3,0.1,-0.2) + yaw 0.2 rad. ent=2: box far behind the plane (must not hit)
        const double eP[3] = {0.2, -0.1, -cs.dist - (cs.ent == 2 ? 4.0 : 0.0)}, eOff[3] = {0.0, 0.0, 0.0}, eH[3] = {0.9, 0.7, 1.0}, eD[3] = {0.3, 0.1, -0.2}, eYaw = 0.2;
        float ed[16] = {(float)eP[0], (float)eP[1], (float)eP[2], (float)eYaw, (float)eOff[0], (float)eOff[1], (float)eOff[2], 0, (float)eH[0], (float)eH[1], (float)eH[2], 0, (float)eD[0], (float)eD[1], (float)eD[2], 0};
        pp.setEntities(ed, cs.ent ? 1 : 0);
        pp.record(cb, color, mats, cd);
        // read back shared mv + depth: copy image -> buffer
        VkBuffer rb; VkDeviceMemory rm; bi.size = (size_t)W * Hh * 8; vkCreateBuffer(dev, &bi, nullptr, &rb); vkGetBufferMemoryRequirements(dev, rb, &mr); mai.allocationSize = mr.size; vkAllocateMemory(dev, &mai, nullptr, &rm); vkBindBufferMemory(dev, rb, rm, 0);
        VkBufferImageCopy rc{}; rc.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; rc.imageExtent = {W, Hh, 1}; rc.bufferOffset = 0; vkCmdCopyImageToBuffer(cb, gb.image(2), VK_IMAGE_LAYOUT_GENERAL, rb, 1, &rc); rc.bufferOffset = (size_t)W * Hh * 4; vkCmdCopyImageToBuffer(cb, gb.image(1), VK_IMAGE_LAYOUT_GENERAL, rb, 1, &rc);
        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER}; mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; mb.dstAccessMask = VK_ACCESS_HOST_READ_BIT; vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr); vkEndCommandBuffer(cb);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &cb; VKCK(vkQueueSubmit(q, 1, &si, nullptr)); vkQueueWaitIdle(q);
        uint8_t* r; vkMapMemory(dev, rm, 0, bi.size, 0, (void**)&r); const uint16_t* mv = (const uint16_t*)r; const float* dd = (const float*)(r + (size_t)W * Hh * 4);
        // CPU reference at a few pixels: world point from jittered==unjittered inverse; prev = rel + delta
        double maxErr = 0, sx = 0, sy = 0; int cnt = 0, hits = 0; for (int y = 10; y < (int)Hh; y += 13) for (int x = 10; x < (int)W; x += 11) {
            double ndc[4] = {(x + .5) / W * 2 - 1, ((y + .5) / Hh * 2 - 1), (double)d, 1}, w4[4]; xf(invVP, ndc, w4); double rel[4] = {w4[0] / w4[3], w4[1] / w4[3], w4[2] / w4[3], 1}; double cur[4], prv[4]; xf(VP, rel, cur); double rp[4] = {rel[0] + delta[0], rel[1] + delta[1], rel[2] + delta[2], 1};
            if (cs.ent) { double q[3] = {rel[0] - eP[0] - eOff[0], rel[1] - eP[1] - eOff[1], rel[2] - eP[2] - eOff[2]};
                if (fabs(q[0]) <= eH[0] && fabs(q[1]) <= eH[1] && fabs(q[2]) <= eH[2]) { double c = cos(eYaw), sn = sin(eYaw), qx = rel[0] - eP[0], qz = rel[2] - eP[2];
                    rp[0] = eP[0] + qx * c + qz * sn - eD[0] + delta[0]; rp[1] = rel[1] - eD[1] + delta[1]; rp[2] = eP[2] + (-qx * sn + qz * c) - eD[2] + delta[2]; ++hits; } } xf(VP, rp, prv);
            double ex = (prv[0] / prv[3] - cur[0] / cur[3]) * 0.5 * W, ey = (prv[1] / prv[3] - cur[1] / cur[3]) * 0.5 * Hh; double gx = h2f(mv[((size_t)y * W + x) * 2]), gy = h2f(mv[((size_t)y * W + x) * 2 + 1]);
            maxErr = fmax(maxErr, fmax(fabs(gx - ex), fabs(gy - ey))); sx += ex; sy += ey; ++cnt; if (fabs(dd[(size_t)y * W + x] - d) > 1e-6) maxErr = 1e9; }
        printf("%-24s entity-hit pixels=%d ", cs.name, hits); if (cs.ent == 1 && hits == 0) { puts("no sample inside the entity box: test is vacuous"); maxErr = 1e9; } printf("depth=%.6f expected mean mv=(%.3f, %.3f) px, max |GPU - CPU| = %.4f px -> %s\n", d, sx / cnt, sy / cnt, maxErr, maxErr < 0.05 ? "OK" : "MISMATCH"); if (maxErr >= 0.05) ++bad;
        vkDestroyBuffer(dev, sb, nullptr); vkDestroyBuffer(dev, rb, nullptr); vkFreeMemory(dev, sm, nullptr); vkFreeMemory(dev, rm, nullptr); vkDestroyCommandPool(dev, pool, nullptr);
    }
    vkDeviceWaitIdle(dev); pp.destroy(); gb.release(); bc.stop(); printf("RESULT: %s\n", bad ? "FAIL" : "PASS"); return bad ? 1 : 0;
}

// Repeated controlled initialisation of the shared GPU planes (allocation + dma-buf import + CRC self-test) to reproduce the intermittent self-test mismatch.
// Usage: crc_stress MODE N [W H]   MODE = realloc (one helper, N allocations, sizes alternate) | restart (new helper per iteration) | same (one helper, same size)
#include "gpu_bridge.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
using namespace fsr4; using namespace fsr4ipc;
#define VKCK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { printf("FAIL %s = %d (line %d)\n", #x, r_, __LINE__); return 20; } } while (0)
int main(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "realloc"; int N = argc > 2 ? atoi(argv[2]) : 20; uint32_t W = argc > 3 ? atoi(argv[3]) : 1280, H = argc > 4 ? atoi(argv[4]) : 720;
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.apiVersion = VK_API_VERSION_1_3; VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &ai; VkInstance inst; VKCK(vkCreateInstance(&ici, nullptr, &inst));
    uint32_t n = 0; vkEnumeratePhysicalDevices(inst, &n, nullptr); std::vector<VkPhysicalDevice> pds(n); vkEnumeratePhysicalDevices(inst, &n, pds.data()); VkPhysicalDevice pd = 0;
    for (auto p : pds) { VkPhysicalDeviceProperties pr; vkGetPhysicalDeviceProperties(p, &pr); if (pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) pd = p; }
    float prio = 1; VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qci.queueCount = 1; qci.pQueuePriorities = &prio; const char* ex[] = {"VK_KHR_external_memory", "VK_KHR_external_memory_fd", "VK_EXT_external_memory_dma_buf"};
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES}; f12.timelineSemaphore = VK_TRUE;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &f12}; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci; dci.enabledExtensionCount = 3; dci.ppEnabledExtensionNames = ex; VkDevice dev; VKCK(vkCreateDevice(pd, &dci, nullptr, &dev)); VkQueue q; vkGetDeviceQueue(dev, 0, 0, &q);
    BridgeConfig cfg; std::string H_ = getenv("HOME"); cfg.provider = "4.1.1"; cfg.protonDir = H_ + "/.steam/steam/compatibilitytools.d/GE-Proton11-6-x86_64"; cfg.prefixDir = H_ + "/.cache/fsr4-linux/pfx_ge"; cfg.steamDir = H_ + "/.local/share/Steam";
    char cwd[512]; getcwd(cwd, sizeof cwd); cfg.helperExe = std::string(cwd) + "/build/probe/fsr4_bridge.exe"; cfg.logFile = cfg.helperExe + ".log"; cfg.transferMode = 2;
    VkCtx ctx{inst, pd, dev, q, 0, vkGetInstanceProcAddr};
    int ok = 0, bad = 0; std::vector<std::string> errs; auto t0 = std::chrono::steady_clock::now();
    BridgeClient* bc = nullptr; GpuBridge* gb = nullptr;
    auto start = [&]() -> bool { bc = new BridgeClient(); if (!bc->start(cfg)) { printf("helper start failed: %s\n", bc->error().c_str()); return false; } gb = new GpuBridge(); std::string e; if (!gb->init(ctx, bc, &e)) { printf("gb init failed: %s\n", e.c_str()); return false; } return true; };
    auto stop = [&]() { if (gb) { gb->release(); delete gb; gb = nullptr; } if (bc) { bc->stop(); delete bc; bc = nullptr; } };
    if (mode != "restart" && !start()) return 2;
    for (int i = 0; i < N; ++i) {
        if (mode == "restart" && !start()) return 2;
        uint32_t w = mode == "realloc" && (i & 1) ? W * 3 / 2 : W, h = mode == "realloc" && (i & 1) ? H * 3 / 2 : H; std::string e;
        bool r = gb->allocate(w / 2, h / 2, w, h, Fmt::RGBA8, Fmt::RGBA8, &e);
        if (r) ++ok; else { ++bad; errs.push_back("iter " + std::to_string(i) + ": " + e); }
        if (mode == "restart") stop();
    }
    stop(); double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("mode=%s N=%d: ok=%d failed=%d (%.1f s)\n", mode.c_str(), N, ok, bad, s); for (auto& e : errs) printf("  %s\n", e.c_str());
    printf("RESULT: %s\n", bad ? "FAIL" : "PASS"); return bad ? 1 : 0;
}

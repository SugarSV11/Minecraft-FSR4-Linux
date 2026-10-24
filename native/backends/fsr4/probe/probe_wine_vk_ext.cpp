// Experiment E3a: which Vulkan external-memory/semaphore extensions does Wine's vulkan-1 expose to a
// Windows app? Decides whether a zero-copy interop path with a native Linux process is even conceivable.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vulkan/vulkan.h>
#include <cstdio>
#include <vector>
#include <cstring>
int main(int argc, char** argv) {
    if (argc > 1) freopen(argv[1], "w", stdout);
    HMODULE m = LoadLibraryA("vulkan-1.dll"); if (!m) { puts("no vulkan-1"); return 1; }
    auto gipa = (PFN_vkGetInstanceProcAddr)GetProcAddress(m, "vkGetInstanceProcAddr");
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ci.pApplicationInfo = &ai;
    VkInstance inst; auto cr = (PFN_vkCreateInstance)gipa(nullptr, "vkCreateInstance");
    printf("vkCreateInstance=%d\n", cr(&ci, nullptr, &inst));
    auto ep = (PFN_vkEnumeratePhysicalDevices)gipa(inst, "vkEnumeratePhysicalDevices");
    uint32_t n = 0; ep(inst, &n, nullptr); std::vector<VkPhysicalDevice> pd(n); ep(inst, &n, pd.data());
    auto gp = (PFN_vkGetPhysicalDeviceProperties)gipa(inst, "vkGetPhysicalDeviceProperties");
    auto ee = (PFN_vkEnumerateDeviceExtensionProperties)gipa(inst, "vkEnumerateDeviceExtensionProperties");
    for (auto d : pd) { VkPhysicalDeviceProperties p; gp(d, &p); printf("device: %s\n", p.deviceName);
        uint32_t c = 0; ee(d, nullptr, &c, nullptr); std::vector<VkExtensionProperties> e(c); ee(d, nullptr, &c, e.data());
        for (auto& x : e) if (strstr(x.extensionName, "external") || strstr(x.extensionName, "dma_buf") || strstr(x.extensionName, "timeline") || strstr(x.extensionName, "win32") || strstr(x.extensionName, "_fd")) printf("  %s\n", x.extensionName); }
    return 0;
}

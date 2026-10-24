// E3e: can native RADV import different kinds of host memory via VK_EXT_external_memory_host?
#include <vulkan/vulkan.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <cstdlib>
int main() {
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.apiVersion = VK_API_VERSION_1_3; VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &ai; VkInstance inst; vkCreateInstance(&ici, nullptr, &inst);
    uint32_t n = 0; vkEnumeratePhysicalDevices(inst, &n, nullptr); std::vector<VkPhysicalDevice> pds(n); vkEnumeratePhysicalDevices(inst, &n, pds.data()); VkPhysicalDevice pd = 0;
    for (auto p : pds) { VkPhysicalDeviceProperties pr; vkGetPhysicalDeviceProperties(p, &pr); if (pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) pd = p; }
    VkPhysicalDeviceExternalMemoryHostPropertiesEXT hp{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT}; VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &hp}; vkGetPhysicalDeviceProperties2(pd, &p2);
    printf("minImportedHostPointerAlignment=%llu\n", (unsigned long long)hp.minImportedHostPointerAlignment);
    float pr = 1; VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; q.queueCount = 1; q.pQueuePriorities = &pr; const char* ex[] = {"VK_EXT_external_memory_host"};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &q; dci.enabledExtensionCount = 1; dci.ppEnabledExtensionNames = ex; VkDevice dev; if (vkCreateDevice(pd, &dci, nullptr, &dev)) return 1;
    auto getProps = (PFN_vkGetMemoryHostPointerPropertiesEXT)vkGetDeviceProcAddr(dev, "vkGetMemoryHostPointerPropertiesEXT");
    const size_t N = 8 << 20;
    auto attempt = [&](const char* name, void* ptr) {
        VkMemoryHostPointerPropertiesEXT mp{VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT}; VkResult r = getProps(dev, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, ptr, &mp);
        printf("%-34s props=%d typeBits=0x%x", name, r, mp.memoryTypeBits); if (r) { printf("\n"); return; }
        int t = __builtin_ctz(mp.memoryTypeBits); VkImportMemoryHostPointerInfoEXT im{VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT}; im.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT; im.pHostPointer = ptr;
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &im}; mai.allocationSize = N; mai.memoryTypeIndex = t; VkDeviceMemory m; r = vkAllocateMemory(dev, &mai, nullptr, &m); printf(" import=%d\n", r); if (!r) vkFreeMemory(dev, m, nullptr); };
    void* anon = mmap(nullptr, N, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); memset(anon, 1, N); attempt("anonymous private", anon);
    void* anons = mmap(nullptr, N, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0); memset(anons, 1, N); attempt("anonymous MAP_SHARED (shmem)", anons);
    int fd = open("/dev/shm/e3e-test", O_RDWR | O_CREAT | O_TRUNC, 0600); ftruncate(fd, N); void* shm = mmap(nullptr, N, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0); memset(shm, 1, N); attempt("/dev/shm file MAP_SHARED", shm); unlink("/dev/shm/e3e-test");
    int mfd = memfd_create("e3e", 0); ftruncate(mfd, N); void* mm = mmap(nullptr, N, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0); memset(mm, 1, N); attempt("memfd MAP_SHARED", mm);
    int hfd = open("/tmp/e3e-test", O_RDWR | O_CREAT | O_TRUNC, 0600); ftruncate(hfd, N); void* fm = mmap(nullptr, N, PROT_READ | PROT_WRITE, MAP_SHARED, hfd, 0); memset(fm, 1, N); attempt("disk file MAP_SHARED (/tmp)", fm); unlink("/tmp/e3e-test");
    return 0;
}

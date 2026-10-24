// E3b (Linux side): launch the Windows probe under GE-Proton, find the dma-buf fd of the shared D3D12 buffer
// inside the Wine process (/proc/<pid>/fd + pidfd_getfd), import it into NATIVE Vulkan (RADV), write a pattern
// through the GPU, then let the D3D12 side verify it. Nothing here assumes interop works: every step reports.
#include <vulkan/vulkan.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434
#endif
#ifndef SYS_pidfd_getfd
#define SYS_pidfd_getfd 438
#endif
#define VK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { printf("FAIL %s = %d (line %d)\n", #x, r_, __LINE__); return 20; } } while (0)

static std::vector<pid_t> scan(const std::string& needle) {
    std::vector<pid_t> v; DIR* d = opendir("/proc");
    while (dirent* e = readdir(d)) { pid_t p = atoi(e->d_name); if (p <= 1) continue; std::ifstream f(std::string("/proc/") + e->d_name + "/cmdline", std::ios::binary);
        std::string c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); if (c.find(needle) != std::string::npos) v.push_back(p); }
    closedir(d); return v;
}
int main(int argc, char** argv) {
    std::string H = getenv("HOME"); std::string dir = argc > 1 ? argv[1] : "build/probe";
    char cwd[1024]; getcwd(cwd, sizeof cwd); std::string abs = std::string(cwd) + "/" + dir;
    std::string syncPath = "/dev/shm/e3b-sync-" + std::to_string(getpid());
    int sfd = open(syncPath.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0600); ftruncate(sfd, 4096);
    volatile uint32_t* sync = (volatile uint32_t*)mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, sfd, 0); memset((void*)sync, 0, 4096);
    pid_t child = fork();
    if (child == 0) { setsid(); setenv("STEAM_COMPAT_DATA_PATH", (H + "/.cache/fsr4-linux/pfx_ge").c_str(), 1); setenv("STEAM_COMPAT_CLIENT_INSTALL_PATH", (H + "/.local/share/Steam").c_str(), 1); setenv("WINEDEBUG", "-all", 1);
        int dn = open("/dev/null", O_RDWR); dup2(dn, 1); dup2(dn, 2);
        std::string proton = H + "/.steam/steam/compatibilitytools.d/GE-Proton11-6-x86_64/proton", exe = abs + "/e3b.exe", log = "Z:" + abs + "/e3b_win.log";
        execlp("python3", "python3", proton.c_str(), "run", exe.c_str(), ("Z:" + syncPath).c_str(), log.c_str(), (char*)nullptr); _exit(127); }
    for (int i = 0; i < 3000 && sync[0] == 0; ++i) usleep(10000);
    if (sync[0] != 1) { printf("helper never created the buffer\n"); kill(-child, SIGKILL); return 1; }
    uint64_t N = (uint64_t)sync[2] << 10; printf("D3D12 side created shared buffer of %llu bytes\n", (unsigned long long)N);

    // locate the wine process and list its dma-buf-ish fds
    pid_t wpid = 0; for (pid_t p : scan("e3b.exe")) { std::ifstream f("/proc/" + std::to_string(p) + "/maps"); std::string l; bool dri = false; while (std::getline(f, l)) if (l.find("libvulkan") != std::string::npos || l.find("d3d12") != std::string::npos) { dri = true; break; } if (dri) wpid = p; }
    printf("wine helper linux pid: %d\n", wpid); if (!wpid) return 2;
    struct Cand { int fd; off_t size; std::string link; };
    std::vector<Cand> cands;
    std::vector<pid_t> scanPids = scan("e3b.exe"); for (pid_t p : scan("wineserver")) scanPids.push_back(p);
    for (pid_t wp : scanPids) {
        int pfd = syscall(SYS_pidfd_open, wp, 0); if (pfd < 0) continue;
        DIR* d = opendir(("/proc/" + std::to_string(wp) + "/fd").c_str()); if (!d) continue;
        while (dirent* e = readdir(d)) { if (e->d_name[0] == '.') continue; char lk[512] = {}; ssize_t n = readlink(("/proc/" + std::to_string(wp) + "/fd/" + e->d_name).c_str(), lk, sizeof lk - 1); if (n <= 0) continue;
            std::string l(lk, n); if (l.find("dmabuf") == std::string::npos) continue;
            int mine = (int)syscall(SYS_pidfd_getfd, pfd, atoi(e->d_name), 0); off_t sz = -1; if (mine >= 0) sz = lseek(mine, 0, SEEK_END);
            printf("  pid %d fd %s -> %s size=%lld (dup'd=%d)\n", wp, e->d_name, l.c_str(), (long long)sz, mine); if (mine >= 0) cands.push_back({mine, sz, l}); }
        closedir(d);
    }
    int shared = -1; for (auto& c : cands) if (c.size >= (off_t)N && c.link.find("dmabuf") != std::string::npos) { shared = c.fd; printf("candidate dma-buf fd (size %lld) selected\n", (long long)c.size); break; }
    if (shared < 0) { printf("NO dma-buf fd of the expected size in the wine process\n"); sync[1] = 9; kill(-child, SIGKILL); return 4; }

    // ---- native Vulkan ----
    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &ai; VkInstance inst; VK(vkCreateInstance(&ici, nullptr, &inst));
    uint32_t n = 0; vkEnumeratePhysicalDevices(inst, &n, nullptr); std::vector<VkPhysicalDevice> pds(n); vkEnumeratePhysicalDevices(inst, &n, pds.data());
    VkPhysicalDevice pd = VK_NULL_HANDLE; for (auto p : pds) { VkPhysicalDeviceProperties pr; vkGetPhysicalDeviceProperties(p, &pr); if (pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { pd = p; printf("native device: %s\n", pr.deviceName); } }
    float prio = 1; VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qci.queueFamilyIndex = 0; qci.queueCount = 1; qci.pQueuePriorities = &prio;
    const char* exts[] = {"VK_KHR_external_memory", "VK_KHR_external_memory_fd", "VK_EXT_external_memory_dma_buf"};
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci; dci.enabledExtensionCount = 3; dci.ppEnabledExtensionNames = exts;
    VkDevice dev; VK(vkCreateDevice(pd, &dci, nullptr, &dev)); VkQueue q; vkGetDeviceQueue(dev, 0, 0, &q);
    VkPhysicalDeviceMemoryProperties mp; vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    auto findType = [&](uint32_t bits, VkMemoryPropertyFlags want) { for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return (int)i; return -1; };
    auto PFN_props = (PFN_vkGetMemoryFdPropertiesKHR)vkGetDeviceProcAddr(dev, "vkGetMemoryFdPropertiesKHR");
    int result = 30; uint32_t round = 0;
    VkBufferCreateInfo sci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; sci.size = N; sci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT; VkBuffer stg; vkCreateBuffer(dev, &sci, nullptr, &stg);
    VkMemoryRequirements sr; vkGetBufferMemoryRequirements(dev, stg, &sr); VkMemoryAllocateInfo sai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; sai.allocationSize = sr.size;
    sai.memoryTypeIndex = findType(sr.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT); VkDeviceMemory smem; VK(vkAllocateMemory(dev, &sai, nullptr, &smem)); vkBindBufferMemory(dev, stg, smem, 0);
    uint32_t* sp; vkMapMemory(dev, smem, 0, N, 0, (void**)&sp); for (size_t i = 0; i < N / 4; ++i) sp[i] = 0xA5000000u ^ (uint32_t)i * 2654435761u; vkUnmapMemory(dev, smem);
    VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; cpi.queueFamilyIndex = 0; VkCommandPool pool; vkCreateCommandPool(dev, &cpi, nullptr, &pool);
    struct U { VkImageUsageFlags u; const char* n; } usages[] = {
        {VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, "SRC|DST|SAMPLED|STORAGE|COLOR"},
        {VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, "SRC|DST|SAMPLED|STORAGE"},
        {VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, "SRC|DST|SAMPLED"},
        {VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, "SRC|DST"},
        {VK_IMAGE_USAGE_TRANSFER_DST_BIT, "DST"},
    };
    bool done = false;
    for (VkExternalMemoryHandleTypeFlagBits ht : {VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT}) for (auto& us : usages) {
        if (done) break;
        printf("--- VkImage import: handleType %s, usage %s, OPTIMAL tiling\n", ht == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT ? "DMA_BUF" : "OPAQUE_FD", us.n);
        int fdcopy = dup(shared);
        VkExternalMemoryImageCreateInfo emi{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO}; emi.handleTypes = ht;
        VkImageCreateInfo ic{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, &emi}; ic.imageType = VK_IMAGE_TYPE_2D; ic.format = VK_FORMAT_R8G8B8A8_UNORM; ic.extent = {2048, 2048, 1}; ic.mipLevels = 1; ic.arrayLayers = 1; ic.samples = VK_SAMPLE_COUNT_1_BIT;
        ic.tiling = VK_IMAGE_TILING_OPTIMAL; ic.usage = us.u; ic.sharingMode = VK_SHARING_MODE_EXCLUSIVE; ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImage img; VkResult r = vkCreateImage(dev, &ic, nullptr, &img); if (r) { printf("  vkCreateImage=%d\n", r); close(fdcopy); continue; }
        VkMemoryRequirements mr; vkGetImageMemoryRequirements(dev, img, &mr); uint32_t bits = mr.memoryTypeBits;
        if (ht == VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT) { VkMemoryFdPropertiesKHR fp{VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR}; PFN_props(dev, ht, fdcopy, &fp); bits &= fp.memoryTypeBits; }
        int mt = findType(bits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); printf("  image mem size=%llu typeBits=0x%x -> type %d\n", (unsigned long long)mr.size, bits, mt); if (mt < 0) { close(fdcopy); continue; }
        VkMemoryDedicatedAllocateInfo ded{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO}; ded.image = img;
        VkImportMemoryFdInfoKHR imp{VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR, &ded}; imp.handleType = ht; imp.fd = fdcopy;
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &imp}; mai.allocationSize = mr.size; mai.memoryTypeIndex = mt;
        VkDeviceMemory mem; r = vkAllocateMemory(dev, &mai, nullptr, &mem); printf("  vkAllocateMemory(import)=%d\n", r); if (r) { close(fdcopy); continue; }
        if (vkBindImageMemory(dev, img, mem, 0)) { printf("  bind failed\n"); continue; }
        VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cai.commandPool = pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 1; VkCommandBuffer cb; vkAllocateCommandBuffers(dev, &cai, &cb);
        VkCommandBufferBeginInfo cbi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkBeginCommandBuffer(cb, &cbi);
        VkImageMemoryBarrier b1{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b1.srcQueueFamilyIndex = b1.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b1.image = img; b1.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b1.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b1.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b1.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b1);
        VkBufferImageCopy bic{}; bic.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; bic.imageExtent = {2048, 2048, 1}; vkCmdCopyBufferToImage(cb, stg, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bic);
        b1.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b1.newLayout = VK_IMAGE_LAYOUT_GENERAL; b1.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b1.dstAccessMask = 0;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b1); vkEndCommandBuffer(cb);
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &cb; VK(vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE)); VK(vkQueueWaitIdle(q));
        ++round; sync[1] = round * 2; for (int i = 0; i < 1500 && sync[3] != round * 2; ++i) usleep(10000);
        printf("  native wrote logical pattern through VkImage; D3D12 verdict: %s\n", sync[3] != round * 2 ? "no answer" : sync[0] == 3 ? "VERIFIED EXACT MATCH" : "MISMATCH");
        if (sync[3] == round * 2 && sync[0] == 3) { result = 0; done = true; }
    }
    sync[1] = 0xFFFF;
    kill(-child, SIGKILL); for (pid_t p : scan("e3b.exe")) kill(p, SIGKILL);
    unlink(syncPath.c_str()); printf("E3b result code %d\n", result); return result;
}

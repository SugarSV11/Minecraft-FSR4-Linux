// E4 (Linux side): can a SHARED D3D12 fence created by VKD3D-Proton under Wine be imported as a native Vulkan TIMELINE semaphore?
// Diffs the fd tables of the Wine processes around CreateSharedHandle(fence), tries vkImportSemaphoreFdKHR on every new fd,
// then tests signalling in both directions and wait-before-signal. Every step reports; nothing is assumed.
#include <vulkan/vulkan.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>
#ifndef SYS_pidfd_open
#define SYS_pidfd_open 434
#endif
#ifndef SYS_pidfd_getfd
#define SYS_pidfd_getfd 438
#endif
static std::vector<pid_t> scan(const std::string& needle) {
    std::vector<pid_t> v; DIR* d = opendir("/proc");
    while (dirent* e = readdir(d)) { pid_t p = atoi(e->d_name); if (p <= 1) continue; std::ifstream f(std::string("/proc/") + e->d_name + "/cmdline", std::ios::binary);
        std::string c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); if (c.find(needle) != std::string::npos) v.push_back(p); }
    closedir(d); return v;
}
using Snap = std::map<std::pair<pid_t, int>, std::string>;
static Snap snapshot() {
    Snap s; std::vector<pid_t> ps = scan("e4.exe"); for (pid_t p : scan("wineserver")) ps.push_back(p);
    for (pid_t p : ps) { DIR* d = opendir(("/proc/" + std::to_string(p) + "/fd").c_str()); if (!d) continue;
        while (dirent* e = readdir(d)) { if (e->d_name[0] == '.') continue; char lk[512] = {}; ssize_t n = readlink(("/proc/" + std::to_string(p) + "/fd/" + e->d_name).c_str(), lk, sizeof lk - 1); if (n > 0) s[{p, atoi(e->d_name)}] = std::string(lk, n); }
        closedir(d); }
    return s;
}
#define VK(x) do { VkResult r_ = (x); if (r_ != VK_SUCCESS) { printf("FAIL %s = %d (line %d)\n", #x, r_, __LINE__); return 20; } } while (0)
int main(int argc, char** argv) {
    std::string H = getenv("HOME"); std::string dir = argc > 1 ? argv[1] : "build/probe"; char cwd[1024]; getcwd(cwd, sizeof cwd); std::string abs = std::string(cwd) + "/" + dir;
    std::string syncPath = "/dev/shm/e4-sync-" + std::to_string(getpid()); int sfd = open(syncPath.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0600); ftruncate(sfd, 4096);
    volatile uint32_t* sync = (volatile uint32_t*)mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, sfd, 0); memset((void*)sync, 0, 4096);
    pid_t child = fork();
    if (child == 0) { setsid(); setenv("STEAM_COMPAT_DATA_PATH", (H + "/.cache/fsr4-linux/pfx_ge").c_str(), 1); setenv("STEAM_COMPAT_CLIENT_INSTALL_PATH", (H + "/.local/share/Steam").c_str(), 1); setenv("WINEDEBUG", "-all,+fixme", 1);
        std::string log = abs + "/e4_stderr.log"; int dn = open("/dev/null", O_RDWR); dup2(dn, 1); int ef = open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644); dup2(ef, 2);
        std::string proton = H + "/.steam/steam/compatibilitytools.d/GE-Proton11-6-x86_64/proton", exe = abs + "/e4.exe", wl = "Z:" + abs + "/e4_win.log";
        execlp("python3", "python3", proton.c_str(), "run", exe.c_str(), ("Z:" + syncPath).c_str(), wl.c_str(), (char*)nullptr); _exit(127); }
    for (int i = 0; i < 3000 && sync[0] == 0; ++i) usleep(10000);
    if (sync[0] != 100) { printf("helper device never ready\n"); kill(-child, SIGKILL); return 1; }
    Snap before = snapshot(); sync[1] = 1;
    for (int i = 0; i < 1000 && sync[0] != 1 && sync[0] != 5; ++i) usleep(10000);
    if (sync[0] != 1) { printf("helper failed to create the shared fence (stage %u)\n", sync[0]); kill(-child, SIGKILL); return 2; }
    Snap after = snapshot();
    struct C { int fd; std::string link; pid_t pid; int num; }; std::vector<C> cands;
    for (auto& kv : after) if (!before.count(kv.first)) {
        int pfd = (int)syscall(SYS_pidfd_open, kv.first.first, 0); int mine = pfd >= 0 ? (int)syscall(SYS_pidfd_getfd, pfd, kv.first.second, 0) : -1; if (pfd >= 0) close(pfd);
        printf("new fd: pid %d fd %d -> %s (dup'd=%d)\n", kv.first.first, kv.first.second, kv.second.c_str(), mine); if (mine >= 0) cands.push_back({mine, kv.second, kv.first.first, kv.first.second}); }
    if (cands.empty()) { printf("NO new fd appeared after CreateSharedHandle(fence)\n"); kill(-child, SIGKILL); return 3; }

    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO}; ai.apiVersion = VK_API_VERSION_1_3;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &ai; VkInstance inst; VK(vkCreateInstance(&ici, nullptr, &inst));
    uint32_t n = 0; vkEnumeratePhysicalDevices(inst, &n, nullptr); std::vector<VkPhysicalDevice> pds(n); vkEnumeratePhysicalDevices(inst, &n, pds.data());
    VkPhysicalDevice pd = VK_NULL_HANDLE; for (auto p : pds) { VkPhysicalDeviceProperties pr; vkGetPhysicalDeviceProperties(p, &pr); if (pr.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) pd = p; }
    VkSemaphoreTypeCreateInfo tci{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO}; tci.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkPhysicalDeviceExternalSemaphoreInfo esi{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_SEMAPHORE_INFO, &tci}; esi.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
    VkExternalSemaphoreProperties esp{VK_STRUCTURE_TYPE_EXTERNAL_SEMAPHORE_PROPERTIES}; vkGetPhysicalDeviceExternalSemaphoreProperties(pd, &esi, &esp);
    printf("native timeline OPAQUE_FD: features=0x%x compatibleHandleTypes=0x%x\n", esp.externalSemaphoreFeatures, esp.compatibleHandleTypes);
    float prio = 1; VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qci.queueFamilyIndex = 0; qci.queueCount = 1; qci.pQueuePriorities = &prio;
    const char* exts[] = {"VK_KHR_external_semaphore", "VK_KHR_external_semaphore_fd"};
    VkPhysicalDeviceVulkan12Features f12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES}; f12.timelineSemaphore = VK_TRUE;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &f12}; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci; dci.enabledExtensionCount = 2; dci.ppEnabledExtensionNames = exts;
    VkDevice dev; VK(vkCreateDevice(pd, &dci, nullptr, &dev));
    auto imp = (PFN_vkImportSemaphoreFdKHR)vkGetDeviceProcAddr(dev, "vkImportSemaphoreFdKHR");
    VkSemaphore sem = VK_NULL_HANDLE; int result = 30;
    for (auto& c : cands) {
        VkExportSemaphoreCreateInfo ex{VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO, &tci}; ex.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &ex}; VkSemaphore s; VK(vkCreateSemaphore(dev, &sci, nullptr, &s));
        VkImportSemaphoreFdInfoKHR ii{VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR}; ii.semaphore = s; ii.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT; ii.fd = dup(c.fd);
        VkResult r = imp(dev, &ii); printf("import candidate pid %d fd %d (%s): %d\n", c.pid, c.num, c.link.c_str(), r);
        if (r == VK_SUCCESS) { sem = s; break; } close(ii.fd); vkDestroySemaphore(dev, s, nullptr);
    }
    if (!sem) { printf("E4 result: no candidate fd could be imported as a timeline semaphore\n"); kill(-child, SIGKILL); for (pid_t p : scan("e4.exe")) kill(p, SIGKILL); return 4; }
    VkSemaphoreSignalInfo sg{VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO}; sg.semaphore = sem; sg.value = 5; VK(vkSignalSemaphore(dev, &sg)); sync[1] = 5;
    for (int i = 0; i < 700 && sync[3] == 0; ++i) usleep(10000);
    printf("native->D3D12: D3D12 saw completed value %u (expected 5); D3D12 queue chain Wait(5)+Signal(7): %s\n", sync[2], sync[3] == 7 ? "completed" : "NOT completed");
    uint64_t cv = 0; vkGetSemaphoreCounterValue(dev, sem, &cv); printf("D3D12->native: native counter value = %llu (expected 7)\n", (unsigned long long)cv);
    uint64_t want = 7; VkSemaphoreWaitInfo wi{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO}; wi.semaphoreCount = 1; wi.pSemaphores = &sem; wi.pValues = &want; VkResult wr = vkWaitSemaphores(dev, &wi, 1000000000ull); printf("vkWaitSemaphores(7) = %d\n", wr);
    // wait-before-signal across APIs: D3D12 queue already waits for 20; signal it from native
    for (int i = 0; i < 300 && sync[0] != 2; ++i) usleep(10000);
    usleep(200000); sg.value = 20; VK(vkSignalSemaphore(dev, &sg)); want = 21; wr = vkWaitSemaphores(dev, &wi, 1000000000ull); vkGetSemaphoreCounterValue(dev, sem, &cv);
    printf("wait-before-signal: D3D12 queue waited for 20, native signalled 20, native wait(21) = %d (counter %llu)\n", wr, (unsigned long long)cv);
    result = (cv >= 21 && sync[2] == 5 && sync[3] == 7) ? 0 : 5;
    sync[1] = 0xFFFF; kill(-child, SIGKILL); for (pid_t p : scan("e4.exe")) kill(p, SIGKILL); unlink(syncPath.c_str());
    printf("E4 result code %d\n", result); return result;
}

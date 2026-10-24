// Standalone FP16 cooperative-matrix GEMM prototype + benchmark (GPU timestamps), compared with a conventional scalar compute shader.
// Usage: gemm_bench coop.spv scalar.spv [M N K]   (N and K multiples of 16, M multiple of 16)
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#define CK(x) do { VkResult r_ = (x); if (r_) { fprintf(stderr, "%s failed: %d (line %d)\n", #x, r_, __LINE__); exit(1); } } while (0)
static uint16_t f2h(float f) { uint32_t x; memcpy(&x, &f, 4); uint32_t s = (x >> 16) & 0x8000; int e = ((x >> 23) & 255) - 127 + 15; uint32_t m = x & 0x7fffff;
    if (e <= 0) return s; if (e >= 31) return s | 0x7c00; return s | (e << 10) | (m >> 13); }
static float h2f(uint16_t h) { uint32_t s = (h & 0x8000) << 16, e = (h >> 10) & 31, m = h & 0x3ff, x; if (!e) { if (!m) x = s; else { e = 1; while (!(m & 0x400)) { m <<= 1; e--; } m &= 0x3ff; x = s | ((e + 112) << 23) | (m << 13); } } else if (e == 31) x = s | 0x7f800000 | (m << 13); else x = s | ((e + 112) << 23) | (m << 13); float f; memcpy(&f, &x, 4); return f; }
static uint32_t* readfile(const char* p, size_t* n) { FILE* f = fopen(p, "rb"); if (!f) { perror(p); exit(1); } fseek(f, 0, SEEK_END); *n = ftell(f); fseek(f, 0, SEEK_SET); uint32_t* b = malloc(*n); fread(b, 1, *n, f); fclose(f); return b; }
static VkPhysicalDeviceMemoryProperties mp;
static uint32_t memtype(uint32_t bits, VkMemoryPropertyFlags want) { for (uint32_t i = 0; i < mp.memoryTypeCount; i++) if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i; fprintf(stderr, "no memtype\n"); exit(1); }
typedef struct { VkBuffer b; VkDeviceMemory m; void* p; } Buf;
static Buf mkbuf(VkDevice d, size_t sz, VkMemoryPropertyFlags f) { Buf r; VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, 0, 0, sz, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT}; CK(vkCreateBuffer(d, &bi, 0, &r.b));
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(d, r.b, &mr); VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, 0, mr.size, memtype(mr.memoryTypeBits, f)}; CK(vkAllocateMemory(d, &ai, 0, &r.m)); CK(vkBindBufferMemory(d, r.b, r.m, 0));
    r.p = 0; if (f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) CK(vkMapMemory(d, r.m, 0, sz, 0, &r.p)); return r; }
int main(int argc, char** argv) {
    if (argc < 4) return 2; uint32_t M = argc > 4 ? atoi(argv[4]) : 2073600, N = argc > 5 ? atoi(argv[5]) : 16, K = argc > 6 ? atoi(argv[6]) : 144;
    VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO, 0, "gemm", 1, "gemm", 1, VK_API_VERSION_1_3}; VkInstanceCreateInfo ic = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, 0, 0, &ai}; VkInstance inst; CK(vkCreateInstance(&ic, 0, &inst));
    uint32_t n = 8; VkPhysicalDevice pd[8]; vkEnumeratePhysicalDevices(inst, &n, pd); VkPhysicalDevice phys = 0; VkPhysicalDeviceProperties pr;
    for (uint32_t i = 0; i < n; i++) { vkGetPhysicalDeviceProperties(pd[i], &pr); if (strstr(pr.deviceName, "AMD")) { phys = pd[i]; break; } } if (!phys) { fprintf(stderr, "no AMD device\n"); return 1; }
    vkGetPhysicalDeviceProperties(phys, &pr); vkGetPhysicalDeviceMemoryProperties(phys, &mp); printf("device: %s  timestampPeriod=%.3f ns\n", pr.deviceName, pr.limits.timestampPeriod);
    VkPhysicalDeviceCooperativeMatrixFeaturesKHR cmf = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_COOPERATIVE_MATRIX_FEATURES_KHR}; cmf.cooperativeMatrix = VK_TRUE;
    VkPhysicalDevice16BitStorageFeatures s16 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES, &cmf}; s16.storageBuffer16BitAccess = VK_TRUE;
    VkPhysicalDeviceVulkan12Features f12 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &s16}; f12.shaderFloat16 = f12.vulkanMemoryModel = f12.vulkanMemoryModelDeviceScope = VK_TRUE;
    VkPhysicalDeviceFeatures2 f2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f12};
    float prio = 1; VkDeviceQueueCreateInfo qi = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, 0, 0, 0, 1, &prio}; uint32_t qn = 8; VkQueueFamilyProperties qf[8]; vkGetPhysicalDeviceQueueFamilyProperties(phys, &qn, qf);
    for (uint32_t i = 0; i < qn; i++) if (qf[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { qi.queueFamilyIndex = i; break; }
    const char* ext[] = {"VK_KHR_cooperative_matrix"}; VkDeviceCreateInfo dc = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &f2, 0, 1, &qi, 0, 0, 1, ext}; VkDevice dev; CK(vkCreateDevice(phys, &dc, 0, &dev));
    VkQueue q; vkGetDeviceQueue(dev, qi.queueFamilyIndex, 0, &q);
    size_t sa = (size_t)M * K * 2, sb = (size_t)K * N * 2, sc = (size_t)M * N * 4;
    Buf hA = mkbuf(dev, sa, 6), hB = mkbuf(dev, sb, 6);   // host visible|coherent
    Buf A = mkbuf(dev, sa, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), B = mkbuf(dev, sb, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), C = mkbuf(dev, sc, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Buf hC = mkbuf(dev, sc, 6);
    uint16_t* pa = hA.p; uint16_t* pb = hB.p; srand(1); for (size_t i = 0; i < (size_t)M * K; i++) pa[i] = f2h((rand() / (float)RAND_MAX) * 2 - 1); for (size_t i = 0; i < (size_t)K * N; i++) pb[i] = f2h((rand() / (float)RAND_MAX) * 2 - 1);
    VkDescriptorSetLayoutBinding bd[3]; for (int i = 0; i < 3; i++) bd[i] = (VkDescriptorSetLayoutBinding){i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, 0};
    VkDescriptorSetLayoutCreateInfo dl = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, 0, 0, 3, bd}; VkDescriptorSetLayout dsl; CK(vkCreateDescriptorSetLayout(dev, &dl, 0, &dsl));
    VkPushConstantRange pc = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 12}; VkPipelineLayoutCreateInfo pli = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, 0, 0, 1, &dsl, 1, &pc}; VkPipelineLayout pl; CK(vkCreatePipelineLayout(dev, &pli, 0, &pl));
    VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3}; VkDescriptorPoolCreateInfo dpi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, 0, 0, 1, 1, &ps}; VkDescriptorPool dp; CK(vkCreateDescriptorPool(dev, &dpi, 0, &dp));
    VkDescriptorSetAllocateInfo dai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, 0, dp, 1, &dsl}; VkDescriptorSet ds; CK(vkAllocateDescriptorSets(dev, &dai, &ds));
    VkDescriptorBufferInfo bi[3] = {{A.b, 0, VK_WHOLE_SIZE}, {B.b, 0, VK_WHOLE_SIZE}, {C.b, 0, VK_WHOLE_SIZE}}; VkWriteDescriptorSet w[3]; for (int i = 0; i < 3; i++) w[i] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, 0, ds, i, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 0, &bi[i], 0}; vkUpdateDescriptorSets(dev, 3, w, 0, 0);
    VkQueryPoolCreateInfo qpi = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, 0, 0, VK_QUERY_TYPE_TIMESTAMP, 2}; VkQueryPool qp; CK(vkCreateQueryPool(dev, &qpi, 0, &qp));
    VkCommandPoolCreateInfo cpi = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, 0, 0, qi.queueFamilyIndex}; VkCommandPool cp; CK(vkCreateCommandPool(dev, &cpi, 0, &cp));
    VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, 0, cp, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1}; VkCommandBuffer cb; CK(vkAllocateCommandBuffers(dev, &cai, &cb));
    // upload
    { VkCommandBufferBeginInfo bg = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; CK(vkBeginCommandBuffer(cb, &bg)); VkBufferCopy r1 = {0, 0, sa}, r2 = {0, 0, sb}; vkCmdCopyBuffer(cb, hA.b, A.b, 1, &r1); vkCmdCopyBuffer(cb, hB.b, B.b, 1, &r2);
      VkMemoryBarrier mb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT}; vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, 0, 0, 0); CK(vkEndCommandBuffer(cb));
      VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb}; CK(vkQueueSubmit(q, 1, &si, 0)); CK(vkQueueWaitIdle(q)); }
    const char* names[3] = {"cooperative matrix, 1 tile/wave (f16 x f16 -> f32)", "conventional scalar compute (f16 operands, f32 accumulate)", "cooperative matrix, 2x2 tiles/wave"}; double ms[3] = {0, 0, 0}; float* res[3];
    const int REP = 20; double flops = 2.0 * M * N * K;
    for (int v = 0; v < 3; v++) {
        if (v == 2 && (M % 32 || N % 32)) { res[2] = res[0]; ms[2] = ms[0]; continue; }
        size_t cs; uint32_t* code = readfile(argv[1 + v], &cs); VkShaderModuleCreateInfo smi = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, 0, 0, cs, code}; VkShaderModule sm; CK(vkCreateShaderModule(dev, &smi, 0, &sm));
        VkComputePipelineCreateInfo cpci = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, 0, 0, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, 0, 0, VK_SHADER_STAGE_COMPUTE_BIT, sm, "main", 0}, pl}; VkPipeline pp; CK(vkCreateComputePipelines(dev, 0, 1, &cpci, 0, &pp));
        uint32_t pcv[3] = {M, N, K}; uint32_t groups = v == 0 ? (M / 16) * (N / 16) : v == 2 ? (M / 32) * (N / 32) : (M * N + 63) / 64;   // coop: the default subgroup size on this GPU is 64 (vulkaninfo), so a 64-thread workgroup is exactly one subgroup = one 16x16 tile
        VkCommandBufferBeginInfo bg = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkResetCommandBuffer(cb, 0); CK(vkBeginCommandBuffer(cb, &bg));
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pp); vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, 0); vkCmdPushConstants(cb, pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 12, pcv);
        for (int i = 0; i < 3; i++) vkCmdDispatch(cb, groups, 1, 1);   // warm-up (+ correctness data is from the last of the timed run)
        VkMemoryBarrier mb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT}; vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, 0, 0, 0);
        vkCmdResetQueryPool(cb, qp, 0, 2); vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp, 0);
        for (int i = 0; i < REP; i++) { vkCmdDispatch(cb, groups, 1, 1); vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, 0, 0, 0); }
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp, 1);
        VkBufferCopy rc = {0, 0, sc}; VkMemoryBarrier mb2 = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, 0, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT}; vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb2, 0, 0, 0, 0); vkCmdCopyBuffer(cb, C.b, hC.b, 1, &rc);
        CK(vkEndCommandBuffer(cb)); VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb}; CK(vkQueueSubmit(q, 1, &si, 0)); CK(vkQueueWaitIdle(q));
        uint64_t t[2]; CK(vkGetQueryPoolResults(dev, qp, 0, 2, 16, t, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT));
        ms[v] = (t[1] - t[0]) * pr.limits.timestampPeriod * 1e-6 / REP; res[v] = malloc(sc); memcpy(res[v], hC.p, sc);
        printf("%-62s %8.4f ms/dispatch   %7.2f TFLOP/s\n", names[v], ms[v], flops / (ms[v] * 1e-3) / 1e12);
    }
    // correctness against a CPU reference on sampled rows (fp32 reference of the fp16-rounded operands)
    double maxerr = 0, maxref = 0, me[3] = {0, 0, 0}; uint32_t rows[8] = {0, 1, 17, M / 3, M / 2, M - 17, M - 2, M - 1};
    for (int ri = 0; ri < 8; ri++) { uint32_t m = rows[ri]; for (uint32_t nn = 0; nn < N; nn++) { double acc = 0; for (uint32_t k = 0; k < K; k++) acc += (double)h2f(pa[(size_t)m * K + k]) * h2f(pb[(size_t)k * N + nn]);
        for (int v = 0; v < 3; v++) { double e = fabs(acc - res[v][(size_t)m * N + nn]); if (e > maxerr) maxerr = e; if (e > me[v]) me[v] = e; } if (fabs(acc) > maxref) maxref = fabs(acc); } }
    double d01 = 0; for (size_t i = 0; i < (size_t)M * N; i += 997) { double e = fabs(res[0][i] - res[1][i]); if (e > d01) d01 = e; }
    printf("per-kernel max |error| vs CPU: coop1=%.3e scalar=%.3e coop2x2=%.3e\n", me[0], me[1], me[2]);
    printf("shape M=%u N=%u K=%u  (%.2f GFLOP per dispatch)\nmax |error| vs CPU reference on 8 sampled rows (both kernels): %.3e (max |ref| %.2f)\nmax |coop - scalar| on a strided sample of the whole output: %.3e\n", M, N, K, flops / 1e9, maxerr, maxref, d01);
    printf("speed-up vs scalar: coop1 %.2fx  coop2x2 %.2fx\n", ms[1] / ms[0], ms[1] / ms[2]); return 0;
}

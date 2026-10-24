// FG x3 prototype driver (standalone Vulkan compute, no game involved): generates the two intermediate frames t=1/3 and t=2/3 between real frames F0 and F1.
// Usage: fgx3 fgx3.spv DIR W H [REPS]    DIR holds c0.rgba c1.rgba d0.f32 d1.f32 mv1.f32 (see scripts/fgx3_dataset.py); writes DIR/out_t1.rgba, DIR/out_t2.rgba
// Reports GPU time per generated frame (timestamps, median of REPS), and the device memory the generator needs.
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define CK(x) do { VkResult r_ = (x); if (r_) { fprintf(stderr, "%s failed: %d (line %d)\n", #x, r_, __LINE__); exit(1); } } while (0)
static VkPhysicalDeviceMemoryProperties mp;
static uint32_t memtype(uint32_t bits, VkMemoryPropertyFlags want) { for (uint32_t i = 0; i < mp.memoryTypeCount; i++) if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i; fprintf(stderr, "no memtype\n"); exit(1); }
typedef struct { VkBuffer b; VkDeviceMemory m; void* p; size_t n; } Buf;
static size_t g_devBytes = 0;
static Buf mkbuf(VkDevice d, size_t sz, int host) { Buf r; r.n = sz; VkBufferCreateInfo bi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, 0, 0, sz, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT}; CK(vkCreateBuffer(d, &bi, 0, &r.b));
    VkMemoryRequirements mr; vkGetBufferMemoryRequirements(d, r.b, &mr); VkMemoryPropertyFlags f = host ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, 0, mr.size, memtype(mr.memoryTypeBits, f)}; CK(vkAllocateMemory(d, &ai, 0, &r.m)); CK(vkBindBufferMemory(d, r.b, r.m, 0));
    r.p = 0; if (host) CK(vkMapMemory(d, r.m, 0, sz, 0, &r.p)); else g_devBytes += mr.size; return r; }
static void* readall(const char* dir, const char* name, size_t n) { char p[512]; snprintf(p, sizeof p, "%s/%s", dir, name); FILE* f = fopen(p, "rb"); if (!f) { perror(p); exit(1); } void* b = malloc(n); if (fread(b, 1, n, f) != n) { fprintf(stderr, "short read %s\n", p); exit(1); } fclose(f); return b; }
static int cmpd(const void* a, const void* b) { double x = *(double*)a, y = *(double*)b; return x < y ? -1 : x > y; }
int main(int argc, char** argv) {
    if (argc < 5) return 2; const char* dir = argv[2]; int W = atoi(argv[3]), H = atoi(argv[4]), REP = argc > 5 ? atoi(argv[5]) : 20; size_t N = (size_t)W * H;
    VkApplicationInfo ai = {VK_STRUCTURE_TYPE_APPLICATION_INFO, 0, "fgx3", 1, "fgx3", 1, VK_API_VERSION_1_3}; VkInstanceCreateInfo ic = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, 0, 0, &ai}; VkInstance inst; CK(vkCreateInstance(&ic, 0, &inst));
    uint32_t n = 8; VkPhysicalDevice pd[8]; vkEnumeratePhysicalDevices(inst, &n, pd); VkPhysicalDevice phys = 0; VkPhysicalDeviceProperties pr;
    for (uint32_t i = 0; i < n; i++) { vkGetPhysicalDeviceProperties(pd[i], &pr); if (strstr(pr.deviceName, "AMD")) { phys = pd[i]; break; } } if (!phys) { fprintf(stderr, "no AMD device\n"); return 1; }
    vkGetPhysicalDeviceMemoryProperties(phys, &mp); printf("device: %s\n", pr.deviceName);
    float prio = 1; VkDeviceQueueCreateInfo qi = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, 0, 0, 0, 1, &prio}; uint32_t qn = 8; VkQueueFamilyProperties qf[8]; vkGetPhysicalDeviceQueueFamilyProperties(phys, &qn, qf);
    for (uint32_t i = 0; i < qn; i++) if (qf[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { qi.queueFamilyIndex = i; break; }
    VkDeviceCreateInfo dc = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, 0, 0, 1, &qi}; VkDevice dev; CK(vkCreateDevice(phys, &dc, 0, &dev)); VkQueue q; vkGetDeviceQueue(dev, qi.queueFamilyIndex, 0, &q);
    size_t szc = N * 4, szm = N * 8; Buf in[5], st[5]; size_t sz[5] = {szc, szc, szc, szc, szm}; const char* nm[5] = {"c0.rgba", "c1.rgba", "d0.f32", "d1.f32", "mv1.f32"};
    Buf ob = mkbuf(dev, szc, 0), zb = mkbuf(dev, szc, 0), ib = mkbuf(dev, szc, 0), hout = mkbuf(dev, szc, 1);
    VkBuffer dbuf[8]; for (int i = 0; i < 5; i++) { st[i] = mkbuf(dev, sz[i], 1); void* d = readall(dir, nm[i], sz[i]); memcpy(st[i].p, d, sz[i]); free(d); in[i] = mkbuf(dev, sz[i], 0); dbuf[i] = in[i].b; } dbuf[5] = ob.b; dbuf[6] = zb.b; dbuf[7] = ib.b;
    VkDescriptorSetLayoutBinding bd[8]; for (int i = 0; i < 8; i++) bd[i] = (VkDescriptorSetLayoutBinding){i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, 0};
    VkDescriptorSetLayoutCreateInfo dl = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, 0, 0, 8, bd}; VkDescriptorSetLayout dsl; CK(vkCreateDescriptorSetLayout(dev, &dl, 0, &dsl));
    VkPushConstantRange pc = {VK_SHADER_STAGE_COMPUTE_BIT, 0, 16}; VkPipelineLayoutCreateInfo pli = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO, 0, 0, 1, &dsl, 1, &pc}; VkPipelineLayout pl; CK(vkCreatePipelineLayout(dev, &pli, 0, &pl));
    VkDescriptorPoolSize ps = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 8}; VkDescriptorPoolCreateInfo dpi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO, 0, 0, 1, 1, &ps}; VkDescriptorPool dp; CK(vkCreateDescriptorPool(dev, &dpi, 0, &dp));
    VkDescriptorSetAllocateInfo dai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, 0, dp, 1, &dsl}; VkDescriptorSet ds; CK(vkAllocateDescriptorSets(dev, &dai, &ds));
    VkDescriptorBufferInfo bi[8]; VkWriteDescriptorSet w[8]; for (int i = 0; i < 8; i++) { bi[i] = (VkDescriptorBufferInfo){dbuf[i], 0, VK_WHOLE_SIZE}; w[i] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, 0, ds, i, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 0, &bi[i]}; } vkUpdateDescriptorSets(dev, 8, w, 0, 0);
    size_t cs; FILE* f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 1; } fseek(f, 0, SEEK_END); cs = ftell(f); fseek(f, 0, SEEK_SET); uint32_t* code = malloc(cs); fread(code, 1, cs, f); fclose(f);
    VkShaderModuleCreateInfo smi = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, 0, 0, cs, code}; VkShaderModule sm; CK(vkCreateShaderModule(dev, &smi, 0, &sm));
    VkComputePipelineCreateInfo cpci = {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, 0, 0, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, 0, 0, VK_SHADER_STAGE_COMPUTE_BIT, sm, "main"}, pl}; VkPipeline pipe; CK(vkCreateComputePipelines(dev, 0, 1, &cpci, 0, &pipe));
    VkQueryPoolCreateInfo qpi = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO, 0, 0, VK_QUERY_TYPE_TIMESTAMP, 4}; VkQueryPool qp; CK(vkCreateQueryPool(dev, &qpi, 0, &qp));
    VkCommandPoolCreateInfo cpi = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, 0, 0, qi.queueFamilyIndex}; VkCommandPool cp; CK(vkCreateCommandPool(dev, &cpi, 0, &cp));
    VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, 0, cp, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1}; VkCommandBuffer cb; CK(vkAllocateCommandBuffers(dev, &cai, &cb));
    VkMemoryBarrier mb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, 0, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT};
    VkBufferCopy cpy; { VkCommandBufferBeginInfo bg = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; CK(vkBeginCommandBuffer(cb, &bg)); for (int i = 0; i < 5; i++) { cpy = (VkBufferCopy){0, 0, sz[i]}; vkCmdCopyBuffer(cb, st[i].b, in[i].b, 1, &cpy); }
      vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, 0, 0, 0); CK(vkEndCommandBuffer(cb)); VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb}; CK(vkQueueSubmit(q, 1, &si, 0)); CK(vkQueueWaitIdle(q)); }
    double* ms = malloc(sizeof(double) * REP);
    for (int rep = 0; rep < REP; rep++) {   // timing: both intermediate frames in one submission, GPU timestamps around everything
        VkCommandBufferBeginInfo bg = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkResetCommandBuffer(cb, 0); CK(vkBeginCommandBuffer(cb, &bg)); vkCmdResetQueryPool(cb, qp, 0, 4);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe); vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, 0);
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, qp, 0);
        for (int fr = 1; fr <= 2; fr++) for (int mode = 0; mode < 4; mode++) { struct { int w, h, mode; float t; } p = {W, H, mode, fr / 3.0f}; vkCmdPushConstants(cb, pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, &p); vkCmdDispatch(cb, (W + 7) / 8, (H + 7) / 8, 1); vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &mb, 0, 0, 0, 0); }
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp, 1); CK(vkEndCommandBuffer(cb)); VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb}; CK(vkQueueSubmit(q, 1, &si, 0)); CK(vkQueueWaitIdle(q));
        uint64_t t[2]; CK(vkGetQueryPoolResults(dev, qp, 0, 2, sizeof t, t, 8, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT)); ms[rep] = (t[1] - t[0]) * pr.limits.timestampPeriod * 1e-6;
    }
    qsort(ms, REP, sizeof(double), cmpd); printf("GPU time for BOTH intermediate frames (t=1/3, t=2/3) at %dx%d: median %.3f ms (min %.3f, max %.3f) => %.3f ms per generated frame\n", W, H, ms[REP / 2], ms[0], ms[REP - 1], ms[REP / 2] / 2);
    for (int fr = 1; fr <= 2; fr++) {   // outputs: one frame per submission
        VkCommandBufferBeginInfo bg = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; vkResetCommandBuffer(cb, 0); CK(vkBeginCommandBuffer(cb, &bg)); vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe); vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &ds, 0, 0);
        for (int mode = 0; mode < 4; mode++) { struct { int w, h, mode; float t; } p = {W, H, mode, fr / 3.0f}; vkCmdPushConstants(cb, pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, &p); vkCmdDispatch(cb, (W + 7) / 8, (H + 7) / 8, 1); vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, 0, 0, 0); }
        cpy = (VkBufferCopy){0, 0, szc}; vkCmdCopyBuffer(cb, ob.b, hout.b, 1, &cpy); VkMemoryBarrier hb = {VK_STRUCTURE_TYPE_MEMORY_BARRIER, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT}; vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &hb, 0, 0, 0, 0);
        CK(vkEndCommandBuffer(cb)); VkSubmitInfo si = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb}; CK(vkQueueSubmit(q, 1, &si, 0)); CK(vkQueueWaitIdle(q));
        char p[512]; snprintf(p, sizeof p, "%s/out_t%d.rgba", dir, fr); FILE* o = fopen(p, "wb"); fwrite(hout.p, 1, szc, o); fclose(o);
    }
    printf("device memory used by the generator (inputs + 3 work buffers): %.1f MiB (inputs: 2 colour + 2 depth + 1 MV = %.1f MiB)\n", g_devBytes / 1048576.0, (4.0 * szc + szm) / 1048576.0);
    return 0;
}

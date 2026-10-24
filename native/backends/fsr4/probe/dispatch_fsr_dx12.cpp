// Experiment E2: real DISPATCH of every provider the official FSR SDK 2.3.0 loader exposes
// (4.1.1 / 3.1.5 / 2.3.4) on a D3D12 device (Wine + VKD3D-Proton on RADV).
// A synthetic aliased scene is rendered on the CPU at render resolution with Halton jitter and exact
// motion vectors; after N frames the output is compared against a supersampled 1920x1080 reference.
// Reports: dispatch return codes, GPU time of the dispatch (timestamp queries), PSNR vs reference.
// Nothing here is a Minecraft metric; it only proves/refutes that the provider executes and reconstructs.
#define WIN32_LEAN_AND_MEAN
#define _WINDOWS
#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <chrono>
#include <ffx_api.h>
#include <ffx_upscale.h>
#include <dx12/ffx_api_dx12.h>
#include <ffx_api_loader.h>

#define CK(x) do { HRESULT _h = (x); if (FAILED(_h)) { printf("FAIL %s hr=0x%08lx line %d\n", #x, (unsigned long)_h, __LINE__); exit(10); } } while (0)
using Comp = ID3D12Device;
static const UINT RW = 1280, RH = 720, DW = 1920, DH = 1080;
static const int FRAMES = 16;

static void msg(uint32_t t, const wchar_t* m) { wprintf(L"[ffx msg type=%u] %ls\n", t, m); }

static uint16_t f2h(float f) {
    uint32_t x; memcpy(&x, &f, 4);
    uint32_t s = (x >> 16) & 0x8000; int e = ((x >> 23) & 0xff) - 127 + 15; uint32_t m = x & 0x7fffff;
    if (e <= 0) return (uint16_t)s;
    if (e >= 31) return (uint16_t)(s | 0x7c00);
    return (uint16_t)(s | (e << 10) | (m >> 13));
}
static float h2f(uint16_t h) {
    uint32_t s = (h & 0x8000) << 16, e = (h >> 10) & 31, m = h & 0x3ff, x;
    if (e == 0) { if (!m) x = s; else { e = 1; while (!(m & 0x400)) { m <<= 1; e--; } m &= 0x3ff; x = s | ((e + 112) << 23) | (m << 13); } }
    else if (e == 31) x = s | 0x7f800000 | (m << 13); else x = s | ((e + 112) << 23) | (m << 13);
    float f; memcpy(&f, &x, 4); return f;
}
// continuous procedural scene; (u,v) in display pixels incl. camera pan
static void scene(float u, float v, float* rgb) {
    int c = ((int)floorf(u / 24.f) + (int)floorf(v / 24.f)) & 1;
    float d = (u + v) / 7.f; d -= floorf(d);
    float st = d < 0.18f ? 1.f : 0.f;
    float rx = u - 700.f, ry = v - 500.f, r = sqrtf(rx * rx + ry * ry) / 9.f; r -= floorf(r);
    float ring = r < 0.2f ? 1.f : 0.f;
    float g = 0.15f + 0.55f * c;
    rgb[0] = fminf(1.f, g + 0.35f * st);
    rgb[1] = fminf(1.f, g * 0.8f + 0.5f * ring);
    rgb[2] = fminf(1.f, 0.2f + 0.3f * (1.f - c) + 0.4f * st * ring + 0.3f * (v / DH));
}
static float halton(int i, int b) { float f = 1, r = 0; while (i > 0) { f /= b; r += f * (i % b); i /= b; } return r; }

struct Gpu {
    ID3D12Device* dev; ID3D12CommandQueue* q; ID3D12CommandAllocator* al; ID3D12GraphicsCommandList* cl;
    ID3D12Fence* fence; HANDLE ev; UINT64 fv = 0; ID3D12QueryHeap* qh; ID3D12Resource* qbuf; UINT64 freq;
    void wait() { q->Signal(fence, ++fv); if (fence->GetCompletedValue() < fv) { fence->SetEventOnCompletion(fv, ev); WaitForSingleObject(ev, INFINITE); } }
};
static ID3D12Resource* mkTex(ID3D12Device* d, DXGI_FORMAT f, UINT w, UINT h, D3D12_RESOURCE_FLAGS fl, D3D12_RESOURCE_STATES st) {
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    D3D12_RESOURCE_DESC rd{D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, w, h, 1, 1, f, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, fl};
    ID3D12Resource* r; CK(d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, st, nullptr, __uuidof(ID3D12Resource), (void**)&r)); return r;
}
static ID3D12Resource* mkBuf(ID3D12Device* d, UINT64 sz, D3D12_HEAP_TYPE t, D3D12_RESOURCE_STATES st) {
    D3D12_HEAP_PROPERTIES hp{t};
    D3D12_RESOURCE_DESC rd{D3D12_RESOURCE_DIMENSION_BUFFER, 0, sz, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
    ID3D12Resource* r; CK(d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, st, nullptr, __uuidof(ID3D12Resource), (void**)&r)); return r;
}
static void barrier(ID3D12GraphicsCommandList* cl, ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
    D3D12_RESOURCE_BARRIER br{}; br.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; br.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, a, b};
    cl->ResourceBarrier(1, &br);
}
static void upload(Gpu& g, ID3D12Resource* dst, ID3D12Resource* up, UINT w, UINT h, UINT bpp, const void* data, D3D12_RESOURCE_STATES back) {
    UINT pitch = (w * bpp + 255) & ~255u; uint8_t* p; D3D12_RANGE nr{0, 0}; CK(up->Map(0, &nr, (void**)&p));
    for (UINT y = 0; y < h; ++y) memcpy(p + (size_t)y * pitch, (const uint8_t*)data + (size_t)y * w * bpp, w * bpp);
    up->Unmap(0, nullptr);
    D3D12_TEXTURE_COPY_LOCATION d{}, s{}; d.pResource = dst; d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; d.SubresourceIndex = 0;
    s.pResource = up; s.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; s.PlacedFootprint = {0, {dst->GetDesc().Format, w, h, 1, pitch}};
    barrier(g.cl, dst, back, D3D12_RESOURCE_STATE_COPY_DEST);
    g.cl->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
    barrier(g.cl, dst, D3D12_RESOURCE_STATE_COPY_DEST, back);
}

int main(int argc, char** argv) {
    if (argc > 2) freopen(argv[2], "w", stdout);
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char* outprefix = argc > 3 ? argv[3] : nullptr;
    HMODULE ffx = LoadLibraryA(argc > 1 ? argv[1] : "amd_fidelityfx_loader_dx12.dll");
    if (!ffx) { printf("FAIL LoadLibrary %lu\n", GetLastError()); return 2; }
    ffxFunctions fn{}; ffxLoadFunctions(&fn, ffx);

    Gpu g{};
    CK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), (void**)&g.dev));
    D3D12_COMMAND_QUEUE_DESC qd{D3D12_COMMAND_LIST_TYPE_DIRECT};
    CK(g.dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&g.q));
    CK(g.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&g.al));
    CK(g.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g.al, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&g.cl));
    g.cl->Close();
    CK(g.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&g.fence)); g.ev = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    D3D12_QUERY_HEAP_DESC qhd{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 2}; CK(g.dev->CreateQueryHeap(&qhd, __uuidof(ID3D12QueryHeap), (void**)&g.qh));
    g.qbuf = mkBuf(g.dev, 4096, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST); g.q->GetTimestampFrequency(&g.freq);

    // providers
    uint64_t count = 0; ffxQueryDescGetVersions q{}; q.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    q.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE; q.device = g.dev; q.outputCount = &count;
    fn.Query(nullptr, &q.header);
    std::vector<uint64_t> ids(count); std::vector<const char*> names(count); q.versionIds = ids.data(); q.versionNames = names.data();
    CK(fn.Query(nullptr, &q.header) == 0 ? S_OK : E_FAIL);
    std::string only = argc > 4 ? argv[4] : "";

    // Reference + per-frame low-res inputs (CPU)
    const float scale = (float)DW / RW;  // 1.5 (Quality)
    std::vector<float> ref((size_t)DW * DH * 3);
    for (UINT y = 0; y < DH; ++y) for (UINT x = 0; x < DW; ++x) {
        float acc[3] = {0, 0, 0};
        float pan = 3.f * (FRAMES - 1);
        for (int sy = 0; sy < 4; ++sy) for (int sx = 0; sx < 4; ++sx) { float c[3]; scene(x + (sx + .5f) / 4 + pan, y + (sy + .5f) / 4, c); for (int k = 0; k < 3; ++k) acc[k] += c[k] / 16; }
        for (int k = 0; k < 3; ++k) ref[((size_t)y * DW + x) * 3 + k] = acc[k];
    }
    auto psnr = [&](const std::vector<uint16_t>& o, UINT pitchPx) {
        double se = 0; size_t n = 0; bool bad = false;
        for (UINT y = 16; y < DH - 16; ++y) for (UINT x = 16; x < DW - 16; ++x) for (int k = 0; k < 3; ++k) {
            float v = h2f(o[(size_t)y * pitchPx + x * 4 + k]); if (!std::isfinite(v)) { bad = true; continue; }
            double d = fminf(fmaxf(v, 0.f), 1.f) - ref[((size_t)y * DW + x) * 3 + k]; se += d * d; ++n; }
        return bad ? -1.0 : 10 * log10(1.0 / (se / n + 1e-12));
    };

    ID3D12Resource *color = mkTex(g.dev, DXGI_FORMAT_R16G16B16A16_FLOAT, RW, RH, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                   *depth = mkTex(g.dev, DXGI_FORMAT_R32_FLOAT, RW, RH, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                   *mv = mkTex(g.dev, DXGI_FORMAT_R16G16_FLOAT, RW, RH, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                   *outT = mkTex(g.dev, DXGI_FORMAT_R16G16B16A16_FLOAT, DW, DH, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12Resource *upC = mkBuf(g.dev, 10240ull * RH, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ),
                   *upD = mkBuf(g.dev, 5120ull * RH, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ),
                   *upM = mkBuf(g.dev, 5120ull * RH, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    ID3D12Resource* rb = mkBuf(g.dev, 15360ull * DH, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    // depth/mv constant: flat plane at 0.5, content pans 3 display px/frame -> mv = +2 render px (cur->prev)
    std::vector<float> depthData((size_t)RW * RH, 0.5f);
    std::vector<uint16_t> mvData((size_t)RW * RH * 2);
    for (size_t i = 0; i < (size_t)RW * RH; ++i) { mvData[i * 2] = f2h((getenv("MVSIGN") ? -1.f : 1.f) * 3.f / scale); mvData[i * 2 + 1] = 0; }

    ffxCreateBackendDX12Desc be{}; be.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; be.device = g.dev;
    int exitc = 0;
    for (uint64_t pi = 0; pi < count; ++pi) {
        if (pi > 0 && ids[pi] == ids[pi - 1]) continue;  // duplicate listing of the same provider id
        if (!only.empty() && only != names[pi]) continue;
        printf("=== provider %s (id 0x%llx)\n", names[pi], (unsigned long long)ids[pi]);
        ffxCreateContextDescUpscaleVersion ver{}; ver.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION; ver.version = FFX_UPSCALER_VERSION;
        ffxOverrideVersion ov{}; ov.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION; ov.versionId = ids[pi];
        ffxCreateContextDescUpscale cd{}; cd.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        cd.flags = FFX_UPSCALE_ENABLE_AUTO_EXPOSURE | FFX_UPSCALE_ENABLE_DEBUG_CHECKING | FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
        cd.maxRenderSize = {RW, RH}; cd.maxUpscaleSize = {DW, DH}; cd.fpMessage = msg;
        cd.header.pNext = &ver.header; ver.header.pNext = &be.header; be.header.pNext = &ov.header;
        ffxContext ctx = nullptr;
        ffxReturnCode_t rc = fn.CreateContext(&ctx, &cd.header, nullptr);
        printf("CreateContext rc=%u\n", rc); if (rc) { exitc = 5; continue; }
        ffxQueryGetProviderVersion pv{}; pv.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION; fn.Query(&ctx, &pv.header);
        printf("active provider: %s (id 0x%llx)\n", pv.versionName ? pv.versionName : "?", (unsigned long long)pv.versionId);
        int32_t phases = 0; ffxQueryDescUpscaleGetJitterPhaseCount jp{}; jp.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
        jp.renderWidth = RW; jp.displayWidth = DW; jp.pOutPhaseCount = &phases; fn.Query(&ctx, &jp.header);
        printf("jitter phase count: %d\n", phases);

        double gpuMs = 0, cpuMs = 0; int disp_ok = 0; std::vector<uint16_t> outData;
        for (int f = 0; f < FRAMES; ++f) {
            float jx = 0, jy = 0; ffxQueryDescUpscaleGetJitterOffset jo{}; jo.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET;
            jo.index = f % (phases ? phases : 8); jo.phaseCount = phases ? phases : 8; jo.pOutX = &jx; jo.pOutY = &jy; fn.Query(&ctx, &jo.header);
            std::vector<uint16_t> col((size_t)RW * RH * 4); float pan = 3.f * f;
            for (UINT y = 0; y < RH; ++y) for (UINT x = 0; x < RW; ++x) {
                float c[3]; scene((x + .5f + jx) * scale + pan, (y + .5f + jy) * scale, c);
                uint16_t* d = &col[((size_t)y * RW + x) * 4]; d[0] = f2h(c[0]); d[1] = f2h(c[1]); d[2] = f2h(c[2]); d[3] = f2h(1.f);
            }
            CK(g.al->Reset()); CK(g.cl->Reset(g.al, nullptr));
            upload(g, color, upC, RW, RH, 8, col.data(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            if (f == 0) { upload(g, depth, upD, RW, RH, 4, depthData.data(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
                          upload(g, mv, upM, RW, RH, 4, mvData.data(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); }
            ffxDispatchDescUpscale dd{}; dd.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE; dd.commandList = g.cl;
            dd.color = ffxApiGetResourceDX12(color, FFX_API_RESOURCE_STATE_COMPUTE_READ);
            dd.depth = ffxApiGetResourceDX12(depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
            dd.motionVectors = ffxApiGetResourceDX12(mv, FFX_API_RESOURCE_STATE_COMPUTE_READ);
            dd.output = ffxApiGetResourceDX12(outT, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
            dd.jitterOffset = {jx, jy}; dd.motionVectorScale = {1.f, 1.f}; dd.renderSize = {RW, RH}; dd.upscaleSize = {DW, DH};
            dd.enableSharpening = false; dd.sharpness = 0.f; dd.frameTimeDelta = 16.6f; dd.preExposure = 1.f; dd.reset = (f == 0);
            dd.cameraNear = 0.1f; dd.cameraFar = 1000.f; dd.cameraFovAngleVertical = 1.0f; dd.viewSpaceToMetersFactor = 1.f; dd.flags = 0;
            g.cl->EndQuery(g.qh, D3D12_QUERY_TYPE_TIMESTAMP, 0);
            rc = fn.Dispatch(&ctx, &dd.header);
            g.cl->EndQuery(g.qh, D3D12_QUERY_TYPE_TIMESTAMP, 1);
            g.cl->ResolveQueryData(g.qh, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, g.qbuf, 0);
            if (rc) { printf("frame %d Dispatch rc=%u\n", f, rc); exitc = 6; g.cl->Close(); break; }
            ++disp_ok;
            if (f == FRAMES - 1) {
                barrier(g.cl, outT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
                D3D12_TEXTURE_COPY_LOCATION d{}, s{}; d.pResource = rb; d.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                d.PlacedFootprint = {0, {DXGI_FORMAT_R16G16B16A16_FLOAT, DW, DH, 1, 15360}};
                s.pResource = outT; s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; s.SubresourceIndex = 0;
                g.cl->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
                barrier(g.cl, outT, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            CK(g.cl->Close());
            auto t0 = std::chrono::high_resolution_clock::now();
            ID3D12CommandList* lists[] = {g.cl}; g.q->ExecuteCommandLists(1, lists); g.wait();
            auto t1 = std::chrono::high_resolution_clock::now();
            UINT64* ts; D3D12_RANGE rr{0, 16}; g.qbuf->Map(0, &rr, (void**)&ts); double ms = (double)(ts[1] - ts[0]) * 1000.0 / g.freq; g.qbuf->Unmap(0, nullptr);
            if (f >= 4) { gpuMs += ms; cpuMs += std::chrono::duration<double, std::milli>(t1 - t0).count(); }
        }
        if (disp_ok == FRAMES) {
            outData.resize((size_t)DW * DH * 4); uint8_t* p; D3D12_RANGE rr{0, 15360ull * DH}; CK(rb->Map(0, &rr, (void**)&p)); memcpy(outData.data(), p, 15360ull * DH); rb->Unmap(0, nullptr);
            double mean = 0; size_t nz = 0; for (size_t i = 0; i < (size_t)DW * DH; ++i) { float v = h2f(outData[i * 4]); mean += v; nz += v != 0; }
            printf("dispatched %d/%d frames OK. output mean(R)=%.4f nonzero=%.1f%%\n", disp_ok, FRAMES, mean / (DW * DH), 100.0 * nz / (DW * DH));
            printf("avg GPU time of dispatch (timestamp, frames 4..%d): %.3f ms ; avg submit+wait wall: %.3f ms\n", FRAMES - 1, gpuMs / (FRAMES - 4), cpuMs / (FRAMES - 4));
            printf("PSNR vs 4x4-supersampled 1080p reference: %.2f dB\n", psnr(outData, DW * 4));
            if (outprefix) { std::string fn2 = std::string(outprefix) + "_" + names[pi] + ".ppm"; FILE* fp = fopen(fn2.c_str(), "wb");
                fprintf(fp, "P6\n%u %u\n255\n", DW, DH); for (size_t i = 0; i < (size_t)DW * DH; ++i) for (int k = 0; k < 3; ++k) fputc((int)(255.f * fminf(fmaxf(h2f(outData[i * 4 + k]), 0.f), 1.f) + .5f), fp); fclose(fp); }
        }
        fn.DestroyContext(&ctx, nullptr);
    }
    // bilinear-free baseline: PSNR of the last frame's raw point-sampled low-res scene (nearest upscale)
    { std::vector<uint16_t> nn((size_t)DW * DH * 4); float pan = 3.f * (FRAMES - 1);
      for (UINT y = 0; y < DH; ++y) for (UINT x = 0; x < DW; ++x) { UINT sx = (UINT)((x + .5f) / scale), sy = (UINT)((y + .5f) / scale); float c[3];
          scene((sx + .5f) * scale + pan, (sy + .5f) * scale, c); for (int k = 0; k < 3; ++k) nn[((size_t)y * DW + x) * 4 + k] = f2h(c[k]); }
      printf("baseline nearest-neighbour upscale of the 720p render: PSNR %.2f dB\n", psnr(nn, DW * 4)); }
    printf("DONE exit=%d\n", exitc);
    return exitc;
}

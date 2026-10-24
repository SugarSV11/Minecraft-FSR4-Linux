// E5-2: standalone FSR Frame Generation 4.0.1 (official AMD DLL, GE-Proton/VKD3D-Proton) with synthetic frames, exact motion vectors and depth.
// Real frames k-1, k are rendered on the CPU from an analytic 3-layer scene; the generated frame is compared with the ANALYTIC frame at t=k-0.5
// (ground truth) and with the naive blend (k-1 + k)/2. Authentic interpolation must beat the blend where things move.
// usage: e5_fg_dispatch.exe <loader dll> <log> <outprefix> [provider-name-prefix] ; env FG_MVSIGN=-1 flips the motion vector sign, FG_FRAMES=n
#define WIN32_LEAN_AND_MEAN
#define _WINDOWS
#include <windows.h>
#include <d3d12.h>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>
#include <ffx_api.h>
#include <ffx_framegeneration.h>
#include <dx12/ffx_api_dx12.h>
#include <ffx_api_loader.h>
#define CK(x) do { HRESULT h_ = (x); if (FAILED(h_)) { printf("FAIL %s hr=0x%08lx line %d\n", #x, (unsigned long)h_, __LINE__); fflush(stdout); return 10; } } while (0)
static void msg(uint32_t t, const wchar_t* m) { wprintf(L"[ffx msg type=%u] %ls\n", t, m); fflush(stdout); }
static UINT W = 1280, H = 720;   // env FG_W / FG_H (the scene is laid out for 1280x720; other sizes are for cost measurements)

static uint16_t f2h(float f) { uint32_t x; memcpy(&x, &f, 4); uint32_t s = (x >> 16) & 0x8000; int e = (int)((x >> 23) & 0xff) - 127 + 15; uint32_t m = x & 0x7fffff;
    if (e <= 0) return (uint16_t)s; if (e >= 31) return (uint16_t)(s | 0x7c00); return (uint16_t)(s | (e << 10) | (m >> 13)); }

struct Px { float c[3]; int layer; float vx, vy; float depth; };   // layer 0 bg, 1 rect, 2 disc; velocity in px/frame (content motion)
static Px shadeAt(float t, float x, float y) {
    Px p; float dx = x - (400 + 30 * t), dy = y - 300, r2 = dx * dx + dy * dy;
    if (r2 < 90.f * 90.f) { float r = sqrtf(r2) / 90.f; p.c[0] = 0.95f - 0.35f * r; p.c[1] = 0.25f + 0.2f * (1 - r); p.c[2] = 0.2f; p.layer = 2; p.vx = 30; p.vy = 0; p.depth = 0.9f; return p; }
    float cx = 900 - 20 * t, cy = 450 + 10 * t; float rx = x - cx, ry = y - cy;
    if (fabsf(rx) < 110 && fabsf(ry) < 70) { int ck = ((int)floorf((rx + 110) / 22) + (int)floorf((ry + 70) / 20)) & 1; p.c[0] = ck ? 0.9f : 0.15f; p.c[1] = ck ? 0.9f : 0.25f; p.c[2] = ck ? 0.95f : 0.8f; p.layer = 1; p.vx = -20; p.vy = 10; p.depth = 0.7f; return p; }
    float u = x - 8 * t; float v = y;   // background pans: content moves +8 px/frame in x
    float s = 0.5f + 0.25f * sinf(u * 0.021f) * cosf(v * 0.017f) + 0.15f * sinf(u * 0.055f + v * 0.043f);
    p.c[0] = 0.15f + 0.5f * s; p.c[1] = 0.25f + 0.45f * s * (0.6f + 0.4f * v / H); p.c[2] = 0.4f + 0.4f * (1 - v / H) * s; p.layer = 0; p.vx = 8; p.vy = 0; p.depth = 0.1f; return p;
}
static void renderFrame(float t, std::vector<uint8_t>& rgba, std::vector<float>* depth, std::vector<uint16_t>* mv, float mvSign) {
    rgba.resize((size_t)W * H * 4); if (depth) depth->resize((size_t)W * H); if (mv) mv->resize((size_t)W * H * 2);
    for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) {
        float acc[3] = {0, 0, 0};
        for (int sy = 0; sy < 2; ++sy) for (int sx = 0; sx < 2; ++sx) { Px p = shadeAt(t, x + (sx + .5f) / 2, y + (sy + .5f) / 2); for (int k = 0; k < 3; ++k) acc[k] += p.c[k] / 4; }
        uint8_t* d = &rgba[((size_t)y * W + x) * 4]; for (int k = 0; k < 3; ++k) d[k] = (uint8_t)(fminf(fmaxf(acc[k], 0.f), 1.f) * 255.f + .5f); d[3] = 255;
        Px c = shadeAt(t, x + .5f, y + .5f);
        if (depth) (*depth)[(size_t)y * W + x] = c.depth;
        if (mv) { (*mv)[((size_t)y * W + x) * 2] = f2h(-c.vx * mvSign); (*mv)[((size_t)y * W + x) * 2 + 1] = f2h(-c.vy * mvSign); }   // FSR convention: previous - current, in pixels
    }
}
static double psnr(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b, const std::vector<uint8_t>* mask, size_t* cnt = nullptr) {
    double se = 0; size_t n = 0;
    for (UINT y = 16; y < H - 16; ++y) for (UINT x = 16; x < W - 16; ++x) { if (mask && !(*mask)[(size_t)y * W + x]) continue; for (int k = 0; k < 3; ++k) { double d = (double)a[((size_t)y * W + x) * 4 + k] - b[((size_t)y * W + x) * 4 + k]; se += d * d; ++n; } }
    if (cnt) *cnt = n / 3; return n ? 10 * log10(255.0 * 255.0 / (se / n + 1e-9)) : 0;
}

struct G { ID3D12Device* dev; ID3D12CommandQueue* q; ID3D12CommandAllocator* al; ID3D12GraphicsCommandList* cl; ID3D12Fence* fence; HANDLE ev; UINT64 fv = 0; ID3D12QueryHeap* qh; ID3D12Resource* qbuf; UINT64 freq = 1;
    void wait() { q->Signal(fence, ++fv); if (fence->GetCompletedValue() < fv) { fence->SetEventOnCompletion(fv, ev); WaitForSingleObject(ev, INFINITE); } } };
static ID3D12Resource* mkTex(ID3D12Device* d, DXGI_FORMAT f, UINT w, UINT h, D3D12_RESOURCE_FLAGS fl, D3D12_RESOURCE_STATES st) {
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT}; D3D12_RESOURCE_DESC rd{D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, w, h, 1, 1, f, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, fl};
    ID3D12Resource* r = nullptr; d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, st, nullptr, __uuidof(ID3D12Resource), (void**)&r); return r; }
static ID3D12Resource* mkBuf(ID3D12Device* d, UINT64 sz, D3D12_HEAP_TYPE t, D3D12_RESOURCE_STATES st) {
    D3D12_HEAP_PROPERTIES hp{t}; D3D12_RESOURCE_DESC rd{D3D12_RESOURCE_DIMENSION_BUFFER, 0, sz, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
    ID3D12Resource* r = nullptr; d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, st, nullptr, __uuidof(ID3D12Resource), (void**)&r); return r; }
static void barrier(ID3D12GraphicsCommandList* cl, ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) { D3D12_RESOURCE_BARRIER br{}; br.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; br.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, a, b}; cl->ResourceBarrier(1, &br); }
static void upload(G& g, ID3D12Resource* dst, ID3D12Resource* up, UINT w, UINT h, UINT bpp, const void* data) {
    UINT pitch = (w * bpp + 255) & ~255u; uint8_t* p; D3D12_RANGE nr{0, 0}; up->Map(0, &nr, (void**)&p); for (UINT y = 0; y < h; ++y) memcpy(p + (size_t)y * pitch, (const uint8_t*)data + (size_t)y * w * bpp, (size_t)w * bpp); up->Unmap(0, nullptr);
    D3D12_TEXTURE_COPY_LOCATION d{}, s{}; d.pResource = dst; d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; s.pResource = up; s.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; s.PlacedFootprint = {0, {dst->GetDesc().Format, w, h, 1, pitch}};
    barrier(g.cl, dst, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST); g.cl->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr); barrier(g.cl, dst, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE); }

int main(int argc, char** argv) {
    if (argc < 4) return 64; freopen(argv[2], "w", stdout); setvbuf(stdout, nullptr, _IONBF, 0);
    std::string outprefix = argv[3]; std::string wantName = argc > 4 ? argv[4] : "4.0.1";
    if (getenv("FG_W")) W = (UINT)atoi(getenv("FG_W")); if (getenv("FG_H")) H = (UINT)atoi(getenv("FG_H")); const UINT PITCH = (W * 4 + 255) & ~255u;
    float mvSign = getenv("FG_MVSIGN") ? (float)atof(getenv("FG_MVSIGN")) : 1.f; int FRAMES = getenv("FG_FRAMES") ? atoi(getenv("FG_FRAMES")) : 8;
    HMODULE ffx = LoadLibraryA(argv[1]); if (!ffx) { printf("FAIL LoadLibrary\n"); return 2; }
    ffxFunctions fn{}; ffxLoadFunctions(&fn, ffx);
    G g{}; CK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), (void**)&g.dev));
    D3D12_COMMAND_QUEUE_DESC qd{D3D12_COMMAND_LIST_TYPE_DIRECT}; CK(g.dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&g.q));
    CK(g.dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&g.al)); CK(g.dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g.al, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&g.cl)); g.cl->Close();
    CK(g.dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&g.fence)); g.ev = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    D3D12_QUERY_HEAP_DESC qhd{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 4}; CK(g.dev->CreateQueryHeap(&qhd, __uuidof(ID3D12QueryHeap), (void**)&g.qh)); g.qbuf = mkBuf(g.dev, 4096, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST); g.q->GetTimestampFrequency(&g.freq);
    ffxConfigureDescGlobalDebug dbg{}; dbg.header.type = FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG; dbg.effectId = FFX_API_EFFECT_ID_FRAMEGENERATION; dbg.fpMessage = msg; dbg.debugLevel = FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_WARNINGS; fn.Configure(nullptr, &dbg.header);

    uint64_t count = 0; ffxQueryDescGetVersions q{}; q.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS; q.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION; q.device = g.dev; q.outputCount = &count; fn.Query(nullptr, &q.header);
    std::vector<uint64_t> ids(count); std::vector<const char*> names(count); q.versionIds = ids.data(); q.versionNames = names.data(); fn.Query(nullptr, &q.header);
    int pi = -1; for (uint64_t i = 0; i < count; ++i) if (!strncmp(names[i], wantName.c_str(), wantName.size())) pi = (int)i; if (pi < 0) { printf("provider %s not offered\n", wantName.c_str()); return 3; }
    printf("provider %s id=0x%llx\n", names[pi], (unsigned long long)ids[pi]);
    ffxCreateBackendDX12Desc be{}; be.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; be.device = g.dev;
    ffxCreateContextDescFrameGenerationVersion ver{}; ver.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION; ver.version = FFX_FRAMEGENERATION_VERSION;
    ffxOverrideVersion ov{}; ov.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION; ov.versionId = ids[pi];
    ffxCreateContextDescFrameGeneration cd{}; cd.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION; cd.flags = FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED | FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING | FFX_FRAMEGENERATION_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS;
    cd.displaySize = {W, H}; cd.maxRenderSize = {W, H}; cd.backBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
    cd.header.pNext = &ver.header; ver.header.pNext = &be.header; be.header.pNext = &ov.header;
    ffxContext ctx = nullptr; ffxReturnCode_t rc = fn.CreateContext(&ctx, &cd.header, nullptr); printf("CreateContext rc=%u\n", rc); if (rc) return 5;

    ID3D12Resource *color = mkTex(g.dev, DXGI_FORMAT_R8G8B8A8_UNORM, W, H, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                   *depth = mkTex(g.dev, DXGI_FORMAT_R32_FLOAT, W, H, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                   *mvT = mkTex(g.dev, DXGI_FORMAT_R16G16_FLOAT, W, H, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
                   *gen = mkTex(g.dev, DXGI_FORMAT_R8G8B8A8_UNORM, W, H, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    ID3D12Resource *upC = mkBuf(g.dev, (uint64_t)PITCH * H, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ), *upD = mkBuf(g.dev, (uint64_t)PITCH * H, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ), *upM = mkBuf(g.dev, (uint64_t)PITCH * H, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ),
                   *rb = mkBuf(g.dev, (uint64_t)PITCH * H, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    std::vector<uint8_t> prevRgba, rgba, gt, genOut((size_t)W * H * 4); std::vector<float> dep; std::vector<uint16_t> mv;
    double sumGpu = 0; int nGpu = 0, genOk = 0;
    for (int f = 0; f < FRAMES; ++f) {
        prevRgba = rgba; renderFrame((float)f, rgba, &dep, &mv, mvSign);
        g.al->Reset(); g.cl->Reset(g.al, nullptr);
        upload(g, color, upC, W, H, 4, rgba.data()); upload(g, depth, upD, W, H, 4, dep.data()); upload(g, mvT, upM, W, H, 4, mv.data());
        // Configure ONCE PER FRAME (frame ids must increase by exactly 1): the provider forces a history reset when frameID != lastConfigureFrameID
        ffxConfigureDescFrameGeneration cf{}; cf.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION; cf.swapChain = nullptr; cf.presentCallback = nullptr; cf.frameGenerationCallback = nullptr;
        cf.frameGenerationEnabled = true; cf.allowAsyncWorkloads = false; cf.flags = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY; cf.onlyPresentGenerated = false; cf.generationRect = {0, 0, (int32_t)W, (int32_t)H}; cf.frameID = (uint64_t)f + 1;
        rc = fn.Configure(&ctx, &cf.header); if (rc) { printf("frame %d Configure rc=%u\n", f, rc); g.cl->Close(); return 9; }
        g.cl->EndQuery(g.qh, D3D12_QUERY_TYPE_TIMESTAMP, 0);
        ffxDispatchDescFrameGenerationPrepareV2 pr{}; pr.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2; pr.frameID = (uint64_t)f + 1; pr.flags = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY; pr.commandList = g.cl;
        pr.renderSize = {W, H}; pr.jitterOffset = {0, 0}; pr.motionVectorScale = {1.f, 1.f}; pr.frameTimeDelta = 16.6f; pr.reset = f == 0; pr.cameraNear = 0.1f; pr.cameraFar = 1000.f; pr.cameraFovAngleVertical = 1.0f; pr.viewSpaceToMetersFactor = 1.f;
        pr.depth = ffxApiGetResourceDX12(depth, FFX_API_RESOURCE_STATE_COMPUTE_READ); pr.motionVectors = ffxApiGetResourceDX12(mvT, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        pr.cameraUp[1] = 1; pr.cameraRight[0] = 1; pr.cameraForward[2] = 1;
        rc = fn.Dispatch(&ctx, &pr.header); if (rc) { printf("frame %d PrepareV2 rc=%u\n", f, rc); g.cl->Close(); return 6; }
        g.cl->EndQuery(g.qh, D3D12_QUERY_TYPE_TIMESTAMP, 1);
        ffxDispatchDescFrameGeneration dd{}; dd.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION; dd.commandList = g.cl;
        dd.presentColor = ffxApiGetResourceDX12(color, FFX_API_RESOURCE_STATE_COMPUTE_READ); dd.outputs[0] = ffxApiGetResourceDX12(gen, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        dd.numGeneratedFrames = 1; dd.reset = f == 0; dd.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB; dd.generationRect = {0, 0, (int32_t)W, (int32_t)H}; dd.frameID = (uint64_t)f + 1;
        rc = fn.Dispatch(&ctx, &dd.header);
        g.cl->EndQuery(g.qh, D3D12_QUERY_TYPE_TIMESTAMP, 2); g.cl->ResolveQueryData(g.qh, D3D12_QUERY_TYPE_TIMESTAMP, 0, 3, g.qbuf, 0);
        if (rc) { printf("frame %d FG Dispatch rc=%u\n", f, rc); g.cl->Close(); return 7; }
        barrier(g.cl, gen, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION d{}, s{}; d.pResource = rb; d.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; d.PlacedFootprint = {0, {DXGI_FORMAT_R8G8B8A8_UNORM, W, H, 1, PITCH}}; s.pResource = gen; s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; g.cl->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
        barrier(g.cl, gen, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS); g.cl->Close();
        ID3D12CommandList* ls[] = {g.cl}; g.q->ExecuteCommandLists(1, ls); g.wait();
        UINT64* ts; D3D12_RANGE rr{0, 24}; g.qbuf->Map(0, &rr, (void**)&ts); double prepMs = (double)(ts[1] - ts[0]) * 1000.0 / g.freq, fgMs = (double)(ts[2] - ts[1]) * 1000.0 / g.freq; g.qbuf->Unmap(0, nullptr);
        uint8_t* p; D3D12_RANGE rr2{0, (uint64_t)PITCH * H}; rb->Map(0, &rr2, (void**)&p); for (UINT y = 0; y < H; ++y) memcpy(genOut.data() + (size_t)y * W * 4, p + (size_t)y * PITCH, (size_t)W * 4); rb->Unmap(0, nullptr);
        if (f < 2) { printf("frame %d: prepare %.3f ms, FG dispatch %.3f ms (no previous frame yet)\n", f, prepMs, fgMs); continue; }
        // analytic ground truth at t = f - 0.5 (the generated frame sits between frame f-1 and f)
        std::vector<uint8_t> gtF; renderFrame((float)f - 0.5f, gtF, nullptr, nullptr, 1.f);
        std::vector<uint8_t> blend((size_t)W * H * 4), mask((size_t)W * H, 0);
        for (size_t i = 0; i < (size_t)W * H; ++i) { for (int k = 0; k < 3; ++k) blend[i * 4 + k] = (uint8_t)((prevRgba[i * 4 + k] + rgba[i * 4 + k] + 1) / 2); blend[i * 4 + 3] = 255; }
        for (UINT y = 0; y < H; ++y) for (UINT x = 0; x < W; ++x) { Px a = shadeAt((float)f - 1, x + .5f, y + .5f), b = shadeAt((float)f, x + .5f, y + .5f), m = shadeAt((float)f - .5f, x + .5f, y + .5f); mask[(size_t)y * W + x] = (a.layer > 0 || b.layer > 0 || m.layer > 0); }
        size_t nm = 0; double pg = psnr(genOut, gtF, nullptr), pb = psnr(blend, gtF, nullptr), pc = psnr(rgba, gtF, nullptr), pgm = psnr(genOut, gtF, &mask, &nm), pbm = psnr(blend, gtF, &mask), pcm = psnr(rgba, gtF, &mask), pgp = psnr(genOut, prevRgba, nullptr), pgc = psnr(genOut, rgba, nullptr);
        printf("frame %d: prepare %.3f ms, FG dispatch %.3f ms | PSNR vs analytic t=%.1f: generated %.2f dB | blend(k-1,k) %.2f | copy of k %.2f || moving-object region (%zu px): generated %.2f | blend %.2f | copy %.2f || generated vs k-1: %.2f dB, vs k: %.2f dB\n", f, prepMs, fgMs, f - 0.5, pg, pb, pc, nm, pgm, pbm, pcm, pgp, pgc);
        if (f >= 3) { sumGpu += fgMs + prepMs; ++nGpu; }
        if (f == FRAMES - 1) { auto ppm = [&](const std::string& n, const std::vector<uint8_t>& im) { FILE* fp = fopen((outprefix + "_" + n + ".ppm").c_str(), "wb"); fprintf(fp, "P6\n%u %u\n255\n", W, H); for (size_t i = 0; i < (size_t)W * H; ++i) fwrite(&im[i * 4], 1, 3, fp); fclose(fp); };
            ppm("generated", genOut); ppm("groundtruth", gtF); ppm("blend", blend); ppm("prev", prevRgba); ppm("cur", rgba); }
        ++genOk;
    }
    if (nGpu) printf("avg GPU time (prepare+FG dispatch, 1280x720): %.3f ms\n", sumGpu / nGpu);
    fn.DestroyContext(&ctx, nullptr); printf("DONE generated frames compared: %d\n", genOk); return genOk ? 0 : 8;
}

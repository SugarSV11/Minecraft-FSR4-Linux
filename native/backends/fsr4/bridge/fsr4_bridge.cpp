// fsr4_bridge.exe - Windows helper that runs under Wine/GE-Proton (VKD3D-Proton). It maps the shared-memory
// file created by the Linux side, validates every request, uploads the planes to D3D12, calls the OFFICIAL AMD
// FidelityFX loader/upscaler DLLs (provider chosen by the Linux side, verified via ffxQuery after creation),
// reads the result back and publishes it. It never substitutes another provider silently.
// usage: fsr4_bridge.exe <mapped file (Windows path)> <log file> [loader dll]
#define WIN32_LEAN_AND_MEAN
#define _WINDOWS
#include <windows.h>
#include <d3d12.h>
#include "d3d12_compat.h"   // ID3D12Device2/3 for mingw-w64 < 12 (Ubuntu 24.04)
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>
#include <ffx_api.h>
#include <ffx_upscale.h>
#include <dx12/ffx_api_dx12.h>
#include <ffx_api_loader.h>
#include <ffx_framegeneration.h>
#include "fsr4_ipc.h"

using namespace fsr4ipc;
using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

static void ffxMsg(uint32_t t, const wchar_t* m) { fwprintf(stdout, L"[ffx type=%u] %ls\n", t, m); fflush(stdout); }
#define LOG(...) do { printf(__VA_ARGS__); printf("\n"); fflush(stdout); } while (0)

static DXGI_FORMAT dxgiOf(Fmt f) {
    switch (f) { case Fmt::RGBA16F: return DXGI_FORMAT_R16G16B16A16_FLOAT; case Fmt::R32F: return DXGI_FORMAT_R32_FLOAT; case Fmt::RG16F: return DXGI_FORMAT_R16G16_FLOAT; case Fmt::RGBA8: return DXGI_FORMAT_R8G8B8A8_UNORM; default: return DXGI_FORMAT_UNKNOWN; }
}

struct Bridge {
    Header* hdr; Request* req; Response* resp; uint8_t* base;
    ffxFunctions fn{};
    ID3D12Device* dev = nullptr; ID3D12CommandQueue* q = nullptr; ID3D12CommandAllocator* al = nullptr;
    ID3D12GraphicsCommandList* cl = nullptr; ID3D12Fence* fence = nullptr; HANDLE ev = nullptr; UINT64 fv = 0;
    ID3D12QueryHeap* qh = nullptr; ID3D12Resource* qbuf = nullptr; UINT64 freq = 1;
    ID3D12Resource *color = nullptr, *depth = nullptr, *mv = nullptr, *out = nullptr, *upC = nullptr, *upD = nullptr, *upM = nullptr, *rb = nullptr;
    uint32_t curRW = 0, curRH = 0, curOW = 0, curOH = 0, curFlags = ~0u;
    std::string curProvName;
    ffxContext ctx = nullptr; uint64_t providerId = 0; std::string providerName;
    ID3D12Heap* hHeap[4] = {}; ID3D12Resource* hBuf[4] = {};   // host-pointer heaps over color/depth/mv/out planes (transferMode 1)
    static constexpr int kShared = 7;   // 0 colour, 1 depth, 2 motion vectors, 3 upscaled output | 4 final colour incl. HUD (FG input), 5 generated frame (FG output), 6 HUD-less colour (world only, FG extracts the UI by comparing it with plane 4)
    ID3D12Resource* shared[kShared] = {}; ID3D12Resource* sharedRb = nullptr; uint32_t sharedW[kShared] = {}, sharedH[kShared] = {}; Fmt sharedFmt[kShared] = {}; HANDLE sharedHandle[kShared] = {};
    ffxContext fgCtx = nullptr; bool tsPendingFg = false; float lastFgMs = 0; uint32_t fgRW = 0, fgRH = 0, fgDW = 0, fgDH = 0; std::string fgProvName;
    ID3D12Fence* sfence = nullptr; HANDLE sfenceHandle = nullptr;   // fence shared with native Vulkan (imported there as a timeline semaphore)
    bool tsPending = false; float lastGpuMs = 0; bool sfenceSignalled = false;
    bool direct = getenv("FSR4_NO_DIRECT") == nullptr;   // default: FFX reads/writes the shared textures in place (no helper-side copies); FSR4_NO_DIRECT=1 restores the copy path
    bool gpuMode = false;   // transferMode 2
    Fmt curColorFmt = Fmt::RGBA16F, curDepthFmt = Fmt::R32F, curMvFmt = Fmt::RG16F, curOutFmt = Fmt::RGBA16F;
    bool hostMode = false; D3D12_RESOURCE_STATES hState[4] = {};
    uint64_t verIds[8]; const char* verNames[8]; uint64_t verCount = 0;

    void fail(const char* m) { snprintf(hdr->error, sizeof hdr->error, "%s", m); LOG("FATAL: %s", m); hdr->helperState.store(kFailed, std::memory_order_release); }
    void wait() { q->Signal(fence, ++fv); if (fence->GetCompletedValue() < fv) { fence->SetEventOnCompletion(fv, ev); WaitForSingleObject(ev, INFINITE); } }
    template <class T> void rel(T*& p) { if (p) { p->Release(); p = nullptr; } }

    ID3D12Resource* tex(DXGI_FORMAT f, UINT w, UINT h, D3D12_RESOURCE_FLAGS fl, D3D12_RESOURCE_STATES st) {
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC rd{D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, w, h, 1, 1, f, {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, fl};
        ID3D12Resource* r = nullptr; if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, st, nullptr, __uuidof(ID3D12Resource), (void**)&r))) return nullptr; return r;
    }
    ID3D12Resource* buf(UINT64 sz, D3D12_HEAP_TYPE t, D3D12_RESOURCE_STATES st) {
        D3D12_HEAP_PROPERTIES hp{t};
        D3D12_RESOURCE_DESC rd{D3D12_RESOURCE_DIMENSION_BUFFER, 0, sz, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
        ID3D12Resource* r = nullptr; if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, st, nullptr, __uuidof(ID3D12Resource), (void**)&r))) return nullptr; return r;
    }
    void barrier(ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) {
        D3D12_RESOURCE_BARRIER br{}; br.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; br.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, a, b}; cl->ResourceBarrier(1, &br);
    }
    static uint32_t pitch256(uint32_t b) { return (b + 255) & ~255u; }

    bool initDevice(const char* loaderDll) {
        HMODULE m = LoadLibraryA(loaderDll);
        if (!m) { fail("LoadLibrary(loader) failed"); return false; }
        ffxLoadFunctions(&fn, m);
        if (!fn.CreateContext || !fn.Query || !fn.Dispatch || !fn.DestroyContext) { fail("loader entry points missing"); return false; }
        if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), (void**)&dev))) { fail("D3D12CreateDevice failed"); return false; }
        // FSR4_QTYPE=compute (experiment): run the upscaler on a D3D12 COMPUTE queue (VKD3D maps it to a compute-family Vulkan queue) instead of the DIRECT queue
        const char* qt = getenv("FSR4_QTYPE"); D3D12_COMMAND_LIST_TYPE lt = (qt && !strcmp(qt, "compute")) ? D3D12_COMMAND_LIST_TYPE_COMPUTE : D3D12_COMMAND_LIST_TYPE_DIRECT;
        D3D12_COMMAND_QUEUE_DESC qd{lt};
        dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&q);
        dev->CreateCommandAllocator(lt, __uuidof(ID3D12CommandAllocator), (void**)&al);
        dev->CreateCommandList(0, lt, al, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&cl); cl->Close();
        dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&fence); ev = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        D3D12_QUERY_HEAP_DESC qhd{D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 4}; dev->CreateQueryHeap(&qhd, __uuidof(ID3D12QueryHeap), (void**)&qh);
        qbuf = buf(4096, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST); q->GetTimestampFrequency(&freq);
        if (hdr->transferMode == 1) { fail("transferMode 1 (D3D12 heap over shared host pages) is BLOCKED: VKD3D falls back to private memory and no data is shared (docs/FSR4_EXPERIMENTS.md E3c-E3e)"); return false; }
        gpuMode = hdr->transferMode == 2;
        if (false) {
            ID3D12Device3* d3 = nullptr;
            if (FAILED(dev->QueryInterface(__uuidof(ID3D12Device3), (void**)&d3)) || !d3) { fail("transferMode 1: ID3D12Device3 unavailable"); return false; }
            const uint64_t offs[4] = {hdr->colorOff, hdr->depthOff, hdr->mvOff, hdr->outOff}, caps[4] = {hdr->colorCap, hdr->depthCap, hdr->mvCap, hdr->outCap};
            // VKD3D-Proton only accepts the BASE address of the mapped region (experiment E3c): one heap over the whole mapping.
            HRESULT hr = d3->OpenExistingHeapFromAddress(base, __uuidof(ID3D12Heap), (void**)&hHeap[0]);
            if (FAILED(hr)) { char m2[160]; snprintf(m2, sizeof m2, "OpenExistingHeapFromAddress(base) hr=0x%08lx", (unsigned long)hr); fail(m2); return false; }
            D3D12_HEAP_DESC hd = hHeap[0]->GetDesc();
            LOG("host heap: size=%llu (mapping %llu) type=%d cpuPage=%d memPool=%d", (unsigned long long)hd.SizeInBytes, (unsigned long long)hdr->totalSize, hd.Properties.Type, hd.Properties.CPUPageProperty, hd.Properties.MemoryPoolPreference);
            for (int i = 0; i < 4; ++i) {
                D3D12_RESOURCE_DESC bd{D3D12_RESOURCE_DIMENSION_BUFFER, 0, caps[i], 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER};
                const D3D12_RESOURCE_STATES tryStates[] = {D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_GENERIC_READ};
                hr = E_FAIL; for (auto st : tryStates) { hr = dev->CreatePlacedResource(hHeap[0], offs[i], &bd, st, nullptr, __uuidof(ID3D12Resource), (void**)&hBuf[i]); LOG("placed plane %d state 0x%x -> hr=0x%08lx", i, (unsigned)st, (unsigned long)hr); if (SUCCEEDED(hr)) { hState[i] = st; break; } }
                if (FAILED(hr)) { char m2[160]; snprintf(m2, sizeof m2, "CreatePlacedResource(plane %d @%llu) hr=0x%08lx", i, (unsigned long long)offs[i], (unsigned long)hr); fail(m2); return false; }
            }
            d3->Release(); hostMode = true; LOG("transferMode 1: planes opened as D3D12 heaps (zero CPU copies in helper)");
        }
        // enumerate providers
        uint64_t count = 0; ffxQueryDescGetVersions qv{}; qv.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        qv.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE; qv.device = dev; qv.outputCount = &count;
        if (fn.Query(nullptr, &qv.header) || !count) { fail("provider enumeration failed"); return false; }
        if (count > 8) count = 8;
        qv.versionIds = verIds; qv.versionNames = verNames; qv.outputCount = &count; verCount = count;
        if (fn.Query(nullptr, &qv.header)) { fail("provider enumeration (2) failed"); return false; }
        std::string list; for (uint64_t i = 0; i < count; ++i) { list += verNames[i]; list += " "; LOG("provider[%llu] %s id=0x%llx", (unsigned long long)i, verNames[i], (unsigned long long)verIds[i]); }
        snprintf(hdr->offered, sizeof hdr->offered, "%s", list.c_str());
        bool found = false;
        for (uint64_t i = 0; i < count; ++i) if (!strcmp(verNames[i], hdr->requestedProvider)) { providerId = verIds[i]; providerName = verNames[i]; found = true; break; }
        if (!found) { char m2[256]; snprintf(m2, sizeof m2, "requested provider %s not offered by runtime (offered: %s)", hdr->requestedProvider, list.c_str()); fail(m2); return false; }
        return true;
    }

    void destroyContext() { if (ctx) { wait(); fn.DestroyContext(&ctx, nullptr); ctx = nullptr; } }
    void freeTargets() { if (color || depth || mv || out || upC || upD || upM || rb) wait(); rel(color); rel(depth); rel(mv); rel(out); rel(upC); rel(upD); rel(upM); rel(rb); }

    bool ensureContext(const Request& r, std::string& err) {
        const char* wantProv = r.provider[0] ? r.provider : hdr->requestedProvider;
        if (ctx && wantProv == curProvName && r.renderW == curRW && r.renderH == curRH && r.outW == curOW && r.outH == curOH && r.flags == curFlags && (Fmt)r.colorFmt == curColorFmt && (Fmt)r.outFmt == curOutFmt) return true;
        uint64_t wantId = 0; bool okProv = false;
        for (uint64_t i = 0; i < verCount; ++i) if (!strcmp(verNames[i], wantProv)) { wantId = verIds[i]; okProv = true; break; }
        if (!okProv) { err = std::string("provider ") + wantProv + " not offered by the runtime (offered: " + hdr->offered + ")"; return false; }
        providerId = wantId; providerName = wantProv;
        destroyContext(); freeTargets();
        curColorFmt = (Fmt)r.colorFmt; curOutFmt = (Fmt)r.outFmt;
        color = tex(dxgiOf(curColorFmt), r.renderW, r.renderH, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        depth = tex(DXGI_FORMAT_R32_FLOAT, r.renderW, r.renderH, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        mv = tex(DXGI_FORMAT_R16G16_FLOAT, r.renderW, r.renderH, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        out = tex(dxgiOf(curOutFmt), r.outW, r.outH, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        upC = buf((UINT64)pitch256(r.renderW * 8) * r.renderH * 1, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        upD = buf((UINT64)pitch256(r.renderW * 4) * r.renderH, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        upM = buf((UINT64)pitch256(r.renderW * 4) * r.renderH, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        rb = buf((UINT64)pitch256(r.outW * 8) * r.outH, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        if (!color || !depth || !mv || !out || !upC || !upD || !upM || !rb) { err = "D3D12 resource allocation failed"; return false; }

        ffxCreateBackendDX12Desc be{}; be.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; be.device = dev;
        ffxCreateContextDescUpscaleVersion ver{}; ver.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION; ver.version = FFX_UPSCALER_VERSION;
        ffxOverrideVersion ov{}; ov.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION; ov.versionId = providerId;
        ffxCreateContextDescUpscale cd{}; cd.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        uint32_t f = 0;
        if (r.flags & kHdr) f |= FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE;
        if (r.flags & kDepthInverted) f |= FFX_UPSCALE_ENABLE_DEPTH_INVERTED;
        if (r.flags & kDepthInfinite) f |= FFX_UPSCALE_ENABLE_DEPTH_INFINITE;
        if (r.flags & kAutoExposure) f |= FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
        if (r.flags & kNonLinearSrgb) f |= FFX_UPSCALE_ENABLE_NON_LINEAR_COLORSPACE;
        cd.flags = f | FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
        cd.maxRenderSize = {r.renderW, r.renderH}; cd.maxUpscaleSize = {r.outW, r.outH}; cd.fpMessage = ffxMsg;
        cd.header.pNext = &ver.header; ver.header.pNext = &be.header; be.header.pNext = &ov.header;
        ffxReturnCode_t rc = fn.CreateContext(&ctx, &cd.header, nullptr);
        if (rc) { char b[96]; snprintf(b, sizeof b, "ffxCreateContext rc=%u", rc); err = b; ctx = nullptr; return false; }
        // PROOF of what runs: ask the runtime which provider this context uses
        ffxQueryGetProviderVersion pv{}; pv.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        rc = fn.Query(&ctx, &pv.header);
        if (rc || pv.versionId != providerId || !pv.versionName || strcmp(pv.versionName, wantProv)) {
            char b[160]; snprintf(b, sizeof b, "provider mismatch: wanted %s got %s (id 0x%llx)", wantProv, pv.versionName ? pv.versionName : "?", (unsigned long long)pv.versionId);
            err = b; destroyContext(); return false;
        }
        snprintf(hdr->activeProvider, sizeof hdr->activeProvider, "%s", pv.versionName); hdr->activeProviderId = pv.versionId;
        curProvName = wantProv; curRW = r.renderW; curRH = r.renderH; curOW = r.outW; curOH = r.outH; curFlags = r.flags;
        LOG("context ready: provider %s id=0x%llx render=%ux%u out=%ux%u flags=0x%x", pv.versionName, (unsigned long long)pv.versionId, r.renderW, r.renderH, r.outW, r.outH, r.flags);
        return true;
    }

    void copyIn(ID3D12Resource* dst, ID3D12Resource* up, uint32_t w, uint32_t h, uint32_t bpp, const uint8_t* src, uint32_t srcPitch) {
        uint32_t pitch = pitch256(w * bpp); uint8_t* p; D3D12_RANGE nr{0, 0}; up->Map(0, &nr, (void**)&p);
        for (uint32_t y = 0; y < h; ++y) memcpy(p + (size_t)y * pitch, src + (size_t)y * srcPitch, (size_t)w * bpp);
        up->Unmap(0, nullptr);
        D3D12_TEXTURE_COPY_LOCATION d{}, s{}; d.pResource = dst; d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        s.pResource = up; s.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; s.PlacedFootprint = {0, {dst->GetDesc().Format, w, h, 1, pitch}};
        barrier(dst, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        cl->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
        barrier(dst, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    void copyInHost(ID3D12Resource* dst, ID3D12Resource* srcBuf, uint32_t w, uint32_t h, uint32_t pitch) {
        D3D12_TEXTURE_COPY_LOCATION d{}, s{}; d.pResource = dst; d.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        s.pResource = srcBuf; s.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; s.PlacedFootprint = {0, {dst->GetDesc().Format, w, h, 1, pitch}};
        barrier(dst, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        cl->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
        barrier(dst, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }
    static bool planeOk(Fmt f, Fmt want, uint32_t w, uint32_t h, uint32_t pitch, uint64_t cap, std::string& e, const char* n) {
        if (f != want) { e = std::string(n) + ": unsupported format"; return false; }
        if (pitch < w * bytesPerPixel(f) || (uint64_t)pitch * h > cap) { e = std::string(n) + ": pitch/size out of bounds"; return false; }
        return true;
    }

    // returns status; fills resp
    // ---- transferMode 2: shared D3D12 textures whose dma-buf is imported by the Linux side --------------------
    ID3D12Resource* mkShared(Fmt f, uint32_t w, uint32_t h, HANDLE* outHandle, D3D12_RESOURCE_FLAGS extra = D3D12_RESOURCE_FLAG_NONE) {
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC rd{D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, w, h, 1, 1, dxgiOf(f), {1, 0}, D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS | extra};
        ID3D12Resource* r = nullptr;
        if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), (void**)&r))) return nullptr;
        HANDLE hnd = nullptr;
        if (FAILED(dev->CreateSharedHandle(r, nullptr, 0, nullptr, &hnd)) || !hnd) { r->Release(); return nullptr; }
        *outHandle = hnd; return r;
    }
    void destroyFg() { if (fgCtx) { wait(); fn.DestroyContext(&fgCtx, nullptr); fgCtx = nullptr; } for (int i = 4; i < kShared; ++i) { if (shared[i]) wait(); rel(shared[i]); if (sharedHandle[i]) { CloseHandle(sharedHandle[i]); sharedHandle[i] = nullptr; } } tsPendingFg = false; }
    void freeShared() {
        if (shared[0] || shared[1] || shared[2] || shared[3]) wait();
        destroyFg();
        for (int i = 0; i < 4; ++i) { rel(shared[i]); if (sharedHandle[i]) { CloseHandle(sharedHandle[i]); sharedHandle[i] = nullptr; } }
    }
    uint32_t allocShared(const Request& r, std::string& msg) {
        LOG("alloc begin render=%ux%u out=%ux%u", r.renderW, r.renderH, r.outW, r.outH);
        if (r.renderW == 0 || r.renderH == 0 || r.outW == 0 || r.outH == 0 || r.renderW > hdr->maxRenderW || r.renderH > hdr->maxRenderH || r.outW > hdr->maxOutW || r.outH > hdr->maxOutH || r.renderW > r.outW || r.renderH > r.outH) { msg = "alloc: dimensions invalid"; return kBadRequest; }
        Fmt f[4] = {(Fmt)r.colorFmt, (Fmt)r.depthFmt, (Fmt)r.mvFmt, (Fmt)r.outFmt};
        if (f[1] != Fmt::R32F || f[2] != Fmt::RG16F || (f[0] != Fmt::RGBA16F && f[0] != Fmt::RGBA8) || (f[3] != Fmt::RGBA16F && f[3] != Fmt::RGBA8)) { msg = "alloc: unsupported formats"; return kBadRequest; }
        destroyContext(); freeTargets(); curRW = 0; freeShared();
        LOG("alloc old resources released");
        uint32_t w[4] = {r.renderW, r.renderW, r.renderW, r.outW}, h[4] = {r.renderH, r.renderH, r.renderH, r.outH};
        for (int i = 0; i < 4; ++i) {
            LOG("alloc shared texture %d begin", i);
            shared[i] = mkShared(f[i], w[i], h[i], &sharedHandle[i], (direct && i == 3) ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE);
            if (!shared[i]) { char b[96]; snprintf(b, sizeof b, "alloc: shared texture %d (%ux%u fmt %u) creation failed", i, w[i], h[i], (unsigned)f[i]); msg = b; freeShared(); return kInternal; }
            LOG("alloc shared texture %d ready", i);
            sharedW[i] = w[i]; sharedH[i] = h[i]; sharedFmt[i] = f[i];
        }
        resp->sharedCount = 4; LOG("alloc: 4 shared textures render=%ux%u out=%ux%u", r.renderW, r.renderH, r.outW, r.outH);
        return kOk;
    }
    uint32_t selfTest(const Request&, std::string& msg) {
        LOG("selftest begin");
        if (!shared[0]) { msg = "selftest: nothing allocated"; return kBadRequest; }
        UINT64 offsets[kShared] = {}; uint32_t pitches[kShared] = {}, bpps[kShared] = {};
        UINT64 total = 0;
        for (int i = 0; i < kShared; ++i) if (shared[i]) {
            bpps[i] = bytesPerPixel(sharedFmt[i]); pitches[i] = pitch256(sharedW[i] * bpps[i]);
            total = alignUp(total, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
            offsets[i] = total; total += (UINT64)pitches[i] * sharedH[i];
        }
        ID3D12Resource* rbk = buf(total, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
        if (!rbk) { msg = "selftest: readback allocation failed"; return kInternal; }
        LOG("selftest: copying %llu bytes across shared planes", (unsigned long long)total);
        HRESULT hr = al->Reset(); if (SUCCEEDED(hr)) hr = cl->Reset(al, nullptr);
        if (FAILED(hr)) { rbk->Release(); msg = "selftest: command list reset failed"; return kInternal; }
        for (int i = 0; i < kShared; ++i) if (shared[i]) {
            D3D12_TEXTURE_COPY_LOCATION d{}, s{};
            d.pResource = rbk; d.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            d.PlacedFootprint = {offsets[i], {dxgiOf(sharedFmt[i]), sharedW[i], sharedH[i], 1, pitches[i]}};
            s.pResource = shared[i]; s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            cl->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
        }
        hr = cl->Close();
        if (FAILED(hr)) { rbk->Release(); msg = "selftest: command list close failed"; return kInternal; }
        ID3D12CommandList* lists[] = {cl}; q->ExecuteCommandLists(1, lists);
        hr = q->Signal(fence, ++fv);
        if (FAILED(hr)) { rbk->Release(); msg = "selftest: D3D12 queue signal failed"; fail(msg.c_str()); return kInternal; }
        if (fence->GetCompletedValue() < fv) {
            hr = fence->SetEventOnCompletion(fv, ev);
            DWORD result = FAILED(hr) ? WAIT_FAILED : WaitForSingleObject(ev, 2500);
            if (result != WAIT_OBJECT_0) {
                char detail[160];
                snprintf(detail, sizeof detail, "selftest: GPU copy timeout (wait=%lu, completed=%llu, removed=0x%08lx)",
                         (unsigned long)result, (unsigned long long)fence->GetCompletedValue(),
                         (unsigned long)dev->GetDeviceRemovedReason());
                msg = detail; rbk->Release(); fail(msg.c_str()); return kInternal;
            }
        }
        LOG("selftest: GPU copy completed");
        const uint8_t* p = nullptr; D3D12_RANGE rr{0, (SIZE_T)total};
        hr = rbk->Map(0, &rr, (void**)&p);
        if (FAILED(hr) || !p) { rbk->Release(); msg = "selftest: readback map failed"; return kInternal; }
        for (int i = 0; i < kShared; ++i) if (shared[i]) {
            uint32_t c = 0;
            for (uint32_t y = 0; y < sharedH[i]; ++y)
                c = crc32(p + offsets[i] + (size_t)y * pitches[i], (size_t)sharedW[i] * bpps[i], c);
            resp->sharedCrc[i] = c;
        }
        rbk->Unmap(0, nullptr); rbk->Release();
        LOG("selftest: CRCs ready");
        return kOk;
    }
    uint32_t fenceInit(std::string& msg) {
        wait(); rel(sfence); if (sfenceHandle) { CloseHandle(sfenceHandle); sfenceHandle = nullptr; }
        if (FAILED(dev->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), (void**)&sfence)) || !sfence) { msg = "shared fence creation failed"; return kInternal; }
        if (FAILED(dev->CreateSharedHandle(sfence, nullptr, GENERIC_ALL, nullptr, &sfenceHandle)) || !sfenceHandle) { rel(sfence); msg = "CreateSharedHandle(fence) failed"; return kInternal; }
        LOG("shared fence created"); return kOk;
    }
    // Probe the imported fd without queuing a GPU Wait. An unrelated fd would leave
    // the D3D12 queue blocked forever, including the fence used by wait().
    uint32_t fenceTest(const Request& r, std::string& msg) {
        if (!sfence) { msg = "no shared fence"; return kBadRequest; }
        auto deadline = Clock::now() + std::chrono::milliseconds(250);
        while (sfence->GetCompletedValue() < r.fenceWait && Clock::now() < deadline) Sleep(1);
        UINT64 observed = sfence->GetCompletedValue();
        if (observed < r.fenceWait) {
            msg = "shared fence fd did not signal (D3D12 counter " + std::to_string(observed) + ")";
            return kInternal;
        }
        if (FAILED(q->Signal(sfence, r.fenceSignal))) { msg = "shared fence queue signal failed"; return kInternal; }
        if (FAILED(q->Signal(fence, ++fv))) { msg = "self test completion signal failed"; return kInternal; }
        if (fence->GetCompletedValue() < fv) {
            if (FAILED(fence->SetEventOnCompletion(fv, ev)) || WaitForSingleObject(ev, 1000) != WAIT_OBJECT_0) {
                msg = "shared fence self test GPU completion timeout"; return kInternal;
            }
        }
        return kOk;
    }
    // asynchronous frames: whatever happens, the native queue is waiting for fenceSignal -> it must always be signalled (after fenceWait to keep the timeline monotonic)
    void signalAfterFailure(const Request& r) {
        if (r.fenceSignal && sfence && !sfenceSignalled) { q->Wait(sfence, r.fenceWait); q->Signal(sfence, r.fenceSignal); sfenceSignalled = true; }
    }

    // ---- frame generation (FSR FG API 4.0.1, official AMD DLL) -------------------------------------------------------------------------------
    static const char* fgName(uint64_t) { return ""; }
    uint32_t fgAlloc(const Request& r, std::string& msg) {
        if (!shared[0] || !shared[1] || !shared[2]) { msg = "fgalloc: base planes not allocated"; return kBadRequest; }
        if (r.outW == 0 || r.outH == 0 || r.outW > hdr->maxOutW || r.outH > hdr->maxOutH) { msg = "fgalloc: bad display size"; return kBadRequest; }
        destroyFg();
        const char* want = r.provider[0] ? r.provider : "4.0.1";
        uint64_t count = 0; ffxQueryDescGetVersions q{}; q.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS; q.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION; q.device = dev; q.outputCount = &count;
        if (fn.Query(nullptr, &q.header) || !count) { msg = "fgalloc: frame generation provider enumeration failed (is amd_fidelityfx_framegeneration_dx12.dll next to the helper?)"; return kContextFailed; }
        if (count > 8) count = 8; uint64_t ids[8]; const char* names[8]; q.versionIds = ids; q.versionNames = names; q.outputCount = &count;
        if (fn.Query(nullptr, &q.header)) { msg = "fgalloc: provider enumeration (2) failed"; return kContextFailed; }
        int pi = -1; std::string offered; for (uint64_t i = 0; i < count; ++i) { offered += names[i]; offered += ' '; if (!strncmp(names[i], want, strlen(want))) pi = (int)i; }
        if (pi < 0) { msg = std::string("fgalloc: provider ") + want + " not offered (offered: " + offered + "); FG 4.0.1 needs AMD's amdxcffx64.dll in the Wine prefix system32 (see docs/FRAME_GENERATION.md)"; return kContextFailed; }
        shared[4] = mkShared(Fmt::RGBA8, r.outW, r.outH, &sharedHandle[4]); shared[5] = mkShared(Fmt::RGBA8, r.outW, r.outH, &sharedHandle[5], D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS); shared[6] = mkShared(Fmt::RGBA8, r.outW, r.outH, &sharedHandle[6]);
        if (!shared[4] || !shared[5] || !shared[6]) { destroyFg(); msg = "fgalloc: shared texture creation failed"; return kInternal; }
        for (int i = 4; i < 7; ++i) { sharedW[i] = r.outW; sharedH[i] = r.outH; sharedFmt[i] = Fmt::RGBA8; }
        ffxCreateBackendDX12Desc be{}; be.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; be.device = dev;
        ffxCreateContextDescFrameGenerationVersion ver{}; ver.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION; ver.version = FFX_FRAMEGENERATION_VERSION;
        ffxOverrideVersion ov{}; ov.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION; ov.versionId = ids[pi];
        ffxCreateContextDescFrameGeneration cd{}; cd.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION; uint32_t f = 0;
        if (r.flags & kDepthInverted) f |= FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED; if (r.flags & kDepthInfinite) f |= FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE; if (r.flags & kHdr) f |= FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE;
        if (getenv("FSR4_FG_DEBUG")) f |= FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING;
        cd.flags = f; cd.displaySize = {r.outW, r.outH}; cd.maxRenderSize = {r.renderW, r.renderH}; cd.backBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
        cd.header.pNext = &ver.header; ver.header.pNext = &be.header; be.header.pNext = &ov.header;
        ffxReturnCode_t rc = fn.CreateContext(&fgCtx, &cd.header, nullptr);
        if (rc) { char b[96]; snprintf(b, sizeof b, "fgalloc: ffxCreateContext(frame generation %s) rc=%u", want, rc); msg = b; fgCtx = nullptr; destroyFg(); return kContextFailed; }
        ffxQueryGetProviderVersion pv{}; pv.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION; fn.Query(&fgCtx, &pv.header);
        fgProvName = pv.versionName ? pv.versionName : ""; fgRW = r.renderW; fgRH = r.renderH; fgDW = r.outW; fgDH = r.outH;
                msg = std::string("provider=") + fgProvName;   // returned to the client in Response.message (what the AMD runtime reports, never assumed)
        resp->sharedCount = 3; LOG("frame generation context ready: provider %s (reported by the runtime: %s) display=%ux%u render=%ux%u flags=0x%x", want, fgProvName.c_str(), r.outW, r.outH, r.renderW, r.renderH, f);
        return kOk;
    }
    uint32_t fgFrame(const Request& r, std::string& msg) {
        sfenceSignalled = false; uint32_t st = fgFrameImpl(r, msg); if (st != kOk) signalAfterFailure(r); return st;
    }
    uint32_t fgFrameImpl(const Request& r, std::string& msg) {
        auto t0 = Clock::now();
        if (!fgCtx || !shared[4] || !shared[5]) { msg = "fgframe: frame generation not allocated"; return kBadRequest; }
        const bool async = r.fenceSignal != 0 && sfence != nullptr;
        if (fence->GetCompletedValue() < fv) { fence->SetEventOnCompletion(fv, ev); WaitForSingleObject(ev, INFINITE); }
        UINT64* ts0; D3D12_RANGE rr0{0, 32}; qbuf->Map(0, &rr0, (void**)&ts0);
        if (tsPending) { lastGpuMs = (float)((double)(ts0[1] - ts0[0]) * 1000.0 / freq); tsPending = false; }
        if (tsPendingFg) { lastFgMs = (float)((double)(ts0[3] - ts0[2]) * 1000.0 / freq); tsPendingFg = false; }
        qbuf->Unmap(0, nullptr);
        al->Reset(); cl->Reset(al, nullptr);
        ffxConfigureDescFrameGeneration cf{}; cf.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION; cf.frameGenerationEnabled = true; cf.allowAsyncWorkloads = false; cf.flags = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY;
        cf.generationRect = {0, 0, (int32_t)fgDW, (int32_t)fgDH}; cf.frameID = r.frameId;
        if (shared[6] && !getenv("FSR4_FG_NO_HUDLESS")) cf.HUDLessColor = ffxApiGetResourceDX12(shared[6], FFX_API_RESOURCE_STATE_COMPUTE_READ);   // UI extraction: FG composes the HUD of the current frame over the generated one
        ffxReturnCode_t rc = fn.Configure(&fgCtx, &cf.header); if (rc) { cl->Close(); char b[64]; snprintf(b, sizeof b, "fg ffxConfigure rc=%u", rc); msg = b; return kDispatchFailed; }
        cl->EndQuery(qh, D3D12_QUERY_TYPE_TIMESTAMP, 2);
        ffxDispatchDescFrameGenerationPrepareV2 pr{}; pr.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2; pr.frameID = r.frameId; pr.flags = FFX_FRAMEGENERATION_FLAG_NO_SWAPCHAIN_CONTEXT_NOTIFY; pr.commandList = cl;
        pr.renderSize = {fgRW, fgRH}; pr.jitterOffset = {r.jitterX, r.jitterY}; pr.motionVectorScale = {r.mvScaleX, r.mvScaleY}; pr.frameTimeDelta = r.frameTimeMs; pr.reset = (r.flags & kReset) != 0;
        pr.cameraNear = r.cameraNear; pr.cameraFar = r.cameraFar; pr.cameraFovAngleVertical = r.fovY; pr.viewSpaceToMetersFactor = 1.f;
        pr.depth = ffxApiGetResourceDX12(shared[1], FFX_API_RESOURCE_STATE_COMPUTE_READ); pr.motionVectors = ffxApiGetResourceDX12(shared[2], FFX_API_RESOURCE_STATE_COMPUTE_READ);
        memcpy(pr.cameraPosition, r.fgCam, 12); memcpy(pr.cameraUp, r.fgCam + 3, 12); memcpy(pr.cameraRight, r.fgCam + 6, 12); memcpy(pr.cameraForward, r.fgCam + 9, 12);
        rc = fn.Dispatch(&fgCtx, &pr.header); if (rc) { cl->Close(); char b[64]; snprintf(b, sizeof b, "fg PrepareV2 rc=%u", rc); msg = b; return kDispatchFailed; }
        ffxDispatchDescFrameGeneration dd{}; dd.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION; dd.commandList = cl;
        dd.presentColor = ffxApiGetResourceDX12(shared[4], FFX_API_RESOURCE_STATE_COMPUTE_READ); dd.outputs[0] = ffxApiGetResourceDX12(shared[5], FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        dd.numGeneratedFrames = 1; dd.reset = (r.flags & kReset) != 0; dd.backbufferTransferFunction = FFX_API_BACKBUFFER_TRANSFER_FUNCTION_SRGB; dd.generationRect = {0, 0, (int32_t)fgDW, (int32_t)fgDH}; dd.frameID = r.frameId;
        rc = fn.Dispatch(&fgCtx, &dd.header); if (rc) { cl->Close(); char b[64]; snprintf(b, sizeof b, "fg Dispatch rc=%u", rc); msg = b; return kDispatchFailed; }
        cl->EndQuery(qh, D3D12_QUERY_TYPE_TIMESTAMP, 3); cl->ResolveQueryData(qh, D3D12_QUERY_TYPE_TIMESTAMP, 2, 2, qbuf, 16);
        cl->Close(); ID3D12CommandList* ls[] = {cl}; double tRec = msSince(t0);
        resp->helperUploadMs = (float)tRec; resp->outPitch = 0; resp->outCrc = 0; resp->gpuDispatchMs = lastFgMs; resp->helperReadbackMs = 0;
        if (async) { q->Wait(sfence, r.fenceWait); q->ExecuteCommandLists(1, ls); q->Signal(sfence, r.fenceSignal); sfenceSignalled = true; q->Signal(fence, ++fv); tsPendingFg = true; return kOk; }
        q->ExecuteCommandLists(1, ls); wait();
        UINT64* ts; D3D12_RANGE rr{0, 32}; qbuf->Map(0, &rr, (void**)&ts); lastFgMs = (float)((double)(ts[3] - ts[2]) * 1000.0 / freq); qbuf->Unmap(0, nullptr); resp->gpuDispatchMs = lastFgMs; return kOk;
    }
    uint32_t handleGpu(const Request& r, std::string& msg) {
        sfenceSignalled = false;
        uint32_t st = handleGpuImpl(r, msg);
        if (st != kOk) signalAfterFailure(r);
        return st;
    }
    uint32_t handleGpuImpl(const Request& r, std::string& msg) {
        auto t0 = Clock::now();
        switch (r.cmd) { case kCmdAlloc: return allocShared(r, msg); case kCmdFree: freeShared(); return kOk; case kCmdSelfTest: return selfTest(r, msg); case kCmdFenceInit: return fenceInit(msg); case kCmdFenceTest: return fenceTest(r, msg); case kCmdFgAlloc: return fgAlloc(r, msg); case kCmdFgFree: destroyFg(); return kOk; case kCmdFgFrame: return fgFrame(r, msg); case kCmdFrame: break; default: msg = "unknown command"; return kBadRequest; }
        const bool async = r.fenceSignal != 0 && sfence != nullptr;
        // previous frame's GPU work must be finished before its command allocator / private textures are reused (normally already is)
        if (fence->GetCompletedValue() < fv) { fence->SetEventOnCompletion(fv, ev); WaitForSingleObject(ev, INFINITE); }
        if (tsPending) { UINT64* ts0; D3D12_RANGE rr0{0, 16}; qbuf->Map(0, &rr0, (void**)&ts0); lastGpuMs = (float)((double)(ts0[1] - ts0[0]) * 1000.0 / freq); qbuf->Unmap(0, nullptr); tsPending = false; }
        if (!shared[0] || r.renderW != sharedW[0] || r.renderH != sharedH[0] || r.outW != sharedW[3] || r.outH != sharedH[3] || (Fmt)r.colorFmt != sharedFmt[0] || (Fmt)r.outFmt != sharedFmt[3]) { msg = "frame: request does not match the allocated shared textures"; return kBadRequest; }
        if (!(r.preExposure > 0.f)) { msg = "preExposure must be > 0"; return kBadRequest; }
        if (!ensureContext(r, msg)) return msg.rfind("provider mismatch", 0) == 0 ? kProviderMismatch : kContextFailed;
        al->Reset(); cl->Reset(al, nullptr);
        ID3D12Resource* privIn[3] = {color, depth, mv};
        if (!direct) {
        for (int i = 0; i < 3; ++i) barrier(privIn[i], D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        for (int i = 0; i < 3; ++i) cl->CopyResource(privIn[i], shared[i]);
        for (int i = 0; i < 3; ++i) barrier(privIn[i], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        ffxDispatchDescUpscale dd{}; dd.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE; dd.commandList = cl;
        dd.color = ffxApiGetResourceDX12(direct ? shared[0] : color, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        dd.depth = ffxApiGetResourceDX12(direct ? shared[1] : depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        dd.motionVectors = ffxApiGetResourceDX12(direct ? shared[2] : mv, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        dd.output = ffxApiGetResourceDX12(direct ? shared[3] : out, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        dd.jitterOffset = {r.jitterX, r.jitterY}; dd.motionVectorScale = {r.mvScaleX, r.mvScaleY};
        dd.renderSize = {r.renderW, r.renderH}; dd.upscaleSize = {r.outW, r.outH};
        dd.enableSharpening = r.enableSharpening != 0; dd.sharpness = r.sharpness; dd.frameTimeDelta = r.frameTimeMs; dd.preExposure = r.preExposure;
        dd.reset = (r.flags & kReset) != 0; dd.cameraNear = r.cameraNear; dd.cameraFar = r.cameraFar; dd.cameraFovAngleVertical = r.fovY; dd.viewSpaceToMetersFactor = 1.f;
        dd.flags = (r.flags & kNonLinearSrgb) ? FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB : 0;
        cl->EndQuery(qh, D3D12_QUERY_TYPE_TIMESTAMP, 0);
        ffxReturnCode_t rc = fn.Dispatch(&ctx, &dd.header);
        cl->EndQuery(qh, D3D12_QUERY_TYPE_TIMESTAMP, 1);
        cl->ResolveQueryData(qh, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, qbuf, 0);
        resp->ffxReturnCode = rc;
        if (rc) { cl->Close(); char b[64]; snprintf(b, sizeof b, "ffxDispatch rc=%u", rc); msg = b; return kDispatchFailed; }
        if (!direct) {
        barrier(out, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cl->CopyResource(shared[3], out);
        barrier(out, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }
        cl->Close(); ID3D12CommandList* ls[] = {cl}; double tRec = msSince(t0);
        if (async) {   // GPU-side dependency chain: native Vulkan signals fenceWait after copy-in, we signal fenceSignal after copy-out; no CPU wait here
            q->Wait(sfence, r.fenceWait); q->ExecuteCommandLists(1, ls); q->Signal(sfence, r.fenceSignal); sfenceSignalled = true; q->Signal(fence, ++fv); tsPending = true;
            resp->gpuDispatchMs = lastGpuMs; resp->helperUploadMs = (float)tRec; resp->helperReadbackMs = 0; resp->outPitch = 0; resp->outCrc = 0; return kOk;
        }
        q->ExecuteCommandLists(1, ls); wait();
        double tWait = msSince(t0) - tRec;
        UINT64* ts; D3D12_RANGE rr{0, 16}; qbuf->Map(0, &rr, (void**)&ts); resp->gpuDispatchMs = (float)((double)(ts[1] - ts[0]) * 1000.0 / freq); qbuf->Unmap(0, nullptr);
        resp->helperUploadMs = (float)tRec; resp->helperReadbackMs = (float)tWait; resp->outPitch = 0; resp->outCrc = 0;   // gpu mode: CPU record time / submit+fence wait time
        (void)t0; return kOk;
    }

    uint32_t handle(const Request& r, std::string& msg) {
        if (gpuMode) return handleGpu(r, msg);
        if (r.cmd != kCmdFrame) { msg = "command requires transferMode 2"; return kBadRequest; }
        auto t0 = Clock::now();
        if (r.renderW == 0 || r.renderH == 0 || r.outW == 0 || r.outH == 0 || r.renderW > hdr->maxRenderW || r.renderH > hdr->maxRenderH || r.outW > hdr->maxOutW || r.outH > hdr->maxOutH || r.renderW > r.outW || r.renderH > r.outH) { msg = "dimensions invalid or exceed mapping"; return kBadRequest; }
        if (!planeOk((Fmt)r.colorFmt, Fmt::RGBA16F, r.renderW, r.renderH, r.colorPitch, hdr->colorCap, msg, "color") ||
            !planeOk((Fmt)r.depthFmt, Fmt::R32F, r.renderW, r.renderH, r.depthPitch, hdr->depthCap, msg, "depth") ||
            !planeOk((Fmt)r.mvFmt, Fmt::RG16F, r.renderW, r.renderH, r.mvPitch, hdr->mvCap, msg, "motion vectors") ||
            (Fmt)r.outFmt != Fmt::RGBA16F) { if (msg.empty()) msg = "output format unsupported"; return kBadRequest; }
        if (!(r.preExposure > 0.f)) { msg = "preExposure must be > 0"; return kBadRequest; }
        const uint8_t* pc = base + hdr->colorOff; const uint8_t* pd = base + hdr->depthOff; const uint8_t* pm = base + hdr->mvOff;
        if (r.colorCrc && (crc32(pc, (size_t)r.colorPitch * r.renderH) | 1u) != r.colorCrc) { msg = "color plane CRC mismatch (shared memory corruption)"; return kBadRequest; }
        if (r.depthCrc && (crc32(pd, (size_t)r.depthPitch * r.renderH) | 1u) != r.depthCrc) { msg = "depth plane CRC mismatch"; return kBadRequest; }
        if (r.mvCrc && (crc32(pm, (size_t)r.mvPitch * r.renderH) | 1u) != r.mvCrc) { msg = "motion vector plane CRC mismatch"; return kBadRequest; }
        if (!ensureContext(r, msg)) return msg.rfind("provider mismatch", 0) == 0 ? kProviderMismatch : kContextFailed;

        al->Reset(); cl->Reset(al, nullptr);
        if (hostMode) {
            if (r.colorPitch % 256 || r.depthPitch % 256 || r.mvPitch % 256) { cl->Close(); msg = "transferMode 1 needs 256-byte aligned pitches"; return kBadRequest; }
            for (int i = 0; i < 3; ++i) barrier(hBuf[i], D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
            copyInHost(color, hBuf[0], r.renderW, r.renderH, r.colorPitch);
            copyInHost(depth, hBuf[1], r.renderW, r.renderH, r.depthPitch);
            copyInHost(mv, hBuf[2], r.renderW, r.renderH, r.mvPitch);
            for (int i = 0; i < 3; ++i) barrier(hBuf[i], D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        } else {
            copyIn(color, upC, r.renderW, r.renderH, 8, pc, r.colorPitch);
            copyIn(depth, upD, r.renderW, r.renderH, 4, pd, r.depthPitch);
            copyIn(mv, upM, r.renderW, r.renderH, 4, pm, r.mvPitch);
        }
        double tUp = msSince(t0);

        ffxDispatchDescUpscale dd{}; dd.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE; dd.commandList = cl;
        dd.color = ffxApiGetResourceDX12(color, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        dd.depth = ffxApiGetResourceDX12(depth, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        dd.motionVectors = ffxApiGetResourceDX12(mv, FFX_API_RESOURCE_STATE_COMPUTE_READ);
        dd.output = ffxApiGetResourceDX12(out, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        dd.jitterOffset = {r.jitterX, r.jitterY}; dd.motionVectorScale = {r.mvScaleX, r.mvScaleY};
        dd.renderSize = {r.renderW, r.renderH}; dd.upscaleSize = {r.outW, r.outH};
        dd.enableSharpening = r.enableSharpening != 0; dd.sharpness = r.sharpness; dd.frameTimeDelta = r.frameTimeMs; dd.preExposure = r.preExposure;
        dd.reset = (r.flags & kReset) != 0; dd.cameraNear = r.cameraNear; dd.cameraFar = r.cameraFar; dd.cameraFovAngleVertical = r.fovY; dd.viewSpaceToMetersFactor = 1.f;
        dd.flags = (r.flags & kNonLinearSrgb) ? FFX_UPSCALE_FLAG_NON_LINEAR_COLOR_SRGB : 0;
        cl->EndQuery(qh, D3D12_QUERY_TYPE_TIMESTAMP, 0);
        ffxReturnCode_t rc = fn.Dispatch(&ctx, &dd.header);
        cl->EndQuery(qh, D3D12_QUERY_TYPE_TIMESTAMP, 1);
        cl->ResolveQueryData(qh, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, qbuf, 0);
        resp->ffxReturnCode = rc;
        if (rc) { cl->Close(); char b[64]; snprintf(b, sizeof b, "ffxDispatch rc=%u", rc); msg = b; return kDispatchFailed; }
        barrier(out, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        uint32_t opitch = pitch256(r.outW * 8);
        if (hostMode && (r.outW * 8) % 256) { cl->Close(); msg = "transferMode 1 needs outW*8 multiple of 256"; return kBadRequest; }
        D3D12_TEXTURE_COPY_LOCATION d{}, s{}; d.pResource = hostMode ? hBuf[3] : rb; d.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; d.PlacedFootprint = {0, {DXGI_FORMAT_R16G16B16A16_FLOAT, r.outW, r.outH, 1, opitch}};
        s.pResource = out; s.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        cl->CopyTextureRegion(&d, 0, 0, 0, &s, nullptr);
        barrier(out, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        cl->Close(); ID3D12CommandList* ls[] = {cl}; q->ExecuteCommandLists(1, ls); wait();
        double tDone = msSince(t0);
        UINT64* ts; D3D12_RANGE rr{0, 16}; qbuf->Map(0, &rr, (void**)&ts); resp->gpuDispatchMs = (float)((double)(ts[1] - ts[0]) * 1000.0 / freq); qbuf->Unmap(0, nullptr);

        uint8_t* po = base + hdr->outOff; uint32_t outPitch = r.outW * 8;
        if (!hostMode) { const uint8_t* src; D3D12_RANGE rr2{0, (SIZE_T)opitch * r.outH}; rb->Map(0, &rr2, (void**)&src);
            for (uint32_t y = 0; y < r.outH; ++y) memcpy(po + (size_t)y * outPitch, src + (size_t)y * opitch, outPitch);
            rb->Unmap(0, nullptr); }
        resp->outPitch = outPitch;
        resp->outCrc = (r.colorCrc || r.depthCrc || r.mvCrc) ? (crc32(po, (size_t)outPitch * r.outH) | 1u) : 0;
        resp->helperUploadMs = (float)tUp; resp->helperReadbackMs = (float)(msSince(t0) - tDone);
        return kOk;
    }
};

int main(int argc, char** argv) {
    if (argc < 3) return 64;
    freopen(argv[2], "a", stdout); setvbuf(stdout, nullptr, _IONBF, 0);
    LOG("helper session pid=%lu", (unsigned long)GetCurrentProcessId());
    HANDLE f = CreateFileA(argv[1], GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) { LOG("cannot open %s err=%lu", argv[1], GetLastError()); return 2; }
    LARGE_INTEGER sz; GetFileSizeEx(f, &sz);
    HANDLE mh = CreateFileMappingA(f, nullptr, PAGE_READWRITE, 0, 0, nullptr);
    uint8_t* base = mh ? (uint8_t*)MapViewOfFile(mh, FILE_MAP_ALL_ACCESS, 0, 0, 0) : nullptr;
    if (!base) { LOG("map failed err=%lu", GetLastError()); return 3; }
    Layout* L = (Layout*)base;
    Bridge b; b.base = base; b.hdr = &L->hdr; b.req = &L->req; b.resp = &L->resp;
    if (L->hdr.magic != kMagic || L->hdr.version != kVersion || L->hdr.totalSize != (uint64_t)sz.QuadPart ||
        L->hdr.outOff + L->hdr.outCap > L->hdr.totalSize || L->hdr.colorOff + L->hdr.colorCap > L->hdr.totalSize ||
        L->hdr.depthOff + L->hdr.depthCap > L->hdr.totalSize || L->hdr.mvOff + L->hdr.mvCap > L->hdr.totalSize) {
        snprintf(L->hdr.error, sizeof L->hdr.error, "bad header (magic/version/size) magic=%x ver=%u total=%llu file=%lld", L->hdr.magic, L->hdr.version, (unsigned long long)L->hdr.totalSize, (long long)sz.QuadPart);
        LOG("%s", L->hdr.error); L->hdr.helperState.store(kFailed); return 4;
    }
    L->hdr.helperPid = GetCurrentProcessId();
    if (!b.initDevice(argc > 3 ? argv[3] : "amd_fidelityfx_loader_dx12.dll")) return 5;
    uint64_t last = L->req.seq.load(std::memory_order_acquire);
    L->hdr.helperState.store(kReady, std::memory_order_release);
    LOG("READY requested provider %s", L->hdr.requestedProvider);
    unsigned spin = 0; auto idleT = Clock::now(); const bool legacyWait = getenv("FSR4_LEGACY_WAIT") != nullptr; const double kSpinMs = getenv("FSR4_SPIN_MS") ? atof(getenv("FSR4_SPIN_MS")) : 20.0; uint64_t lastBeat = L->hdr.clientBeat.load(); auto beatT = Clock::now();
    for (;;) {
        if (legacyWait || (++spin & 255) == 0) L->hdr.heartbeat.fetch_add(1, std::memory_order_relaxed);
        uint64_t s = L->req.seq.load(std::memory_order_acquire);
        if (legacyWait) ++spin;
        if (s == last) {
            // Busy-poll for kSpinMs after the last request (frames arrive every few ms while playing): Sleep(1) costs ~1 ms of wake latency.
            if (legacyWait) { if (spin < 4000) YieldProcessor(); else Sleep(1); }   // FSR4_LEGACY_WAIT=1: previous (slower) idle policy
            else { YieldProcessor(); if ((spin & 1023) == 0 && msSince(idleT) > kSpinMs) Sleep(1); }
            if ((spin & 255) == 0 && L->hdr.leaseTimeoutMs) {   // orphan protection: client stopped renewing its lease
                uint64_t cb = L->hdr.clientBeat.load(std::memory_order_acquire);
                if (cb != lastBeat) { lastBeat = cb; beatT = Clock::now(); }
                else if (msSince(beatT) > L->hdr.leaseTimeoutMs) { LOG("client lease expired (%u ms) -> exiting", L->hdr.leaseTimeoutMs); break; }
            }
            continue;
        }
        spin = 0;
        if (s == UINT64_MAX) break;  // shutdown request
        Request r; memcpy(&r, (const void*)&L->req, sizeof r);  // snapshot (seq copied but unused)
        memset(L->resp.message, 0, sizeof L->resp.message);
        auto t0 = Clock::now(); std::string msg;
        uint32_t st = b.handle(r, msg);
        L->resp.status = st; L->resp.frameId = r.frameId; L->resp.helperTotalMs = (float)msSince(t0);
        snprintf(L->resp.message, sizeof L->resp.message, "%s", msg.c_str());
        if (st) LOG("frame %llu status=%u %s", (unsigned long long)r.frameId, st, msg.c_str());
        last = s;
        L->resp.seq.store(s, std::memory_order_release); idleT = Clock::now();
    }
    b.destroyContext(); b.freeTargets(); b.rel(b.sfence);
    L->hdr.helperState.store(kShutdown, std::memory_order_release);
    LOG("shutdown clean");
    return 0;
}

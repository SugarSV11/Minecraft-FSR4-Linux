// E5-1: does the OFFICIAL FSR Frame Generation (API 4.0.1, SDK 2.3.0) DLL load under GE-Proton/VKD3D-Proton, which providers does it
// offer on this GPU, and can a frame generation context (no swapchain) be created? NO dispatch here: context creation only.
#define WIN32_LEAN_AND_MEAN
#define _WINDOWS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <vector>
#include <ffx_api.h>
#include <ffx_framegeneration.h>
#include <dx12/ffx_api_dx12.h>
#include <ffx_api_loader.h>
static void msg(uint32_t t, const wchar_t* m) { wprintf(L"[ffx msg type=%u] %ls\n", t, m); fflush(stdout); }
int main(int argc, char** argv) {
    if (argc > 2) freopen(argv[2], "w", stdout);
    setvbuf(stdout, nullptr, _IONBF, 0);
    HMODULE ffx = LoadLibraryA(argc > 1 ? argv[1] : "amd_fidelityfx_loader_dx12.dll");
    if (!ffx) { printf("FAIL LoadLibrary err=%lu\n", GetLastError()); return 2; }
    ffxFunctions fn{}; ffxLoadFunctions(&fn, ffx);
    printf("entry points: create=%p query=%p dispatch=%p configure=%p\n", (void*)fn.CreateContext, (void*)fn.Query, (void*)fn.Dispatch, (void*)fn.Configure);
    ID3D12Device* dev = nullptr; HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), (void**)&dev);
    printf("D3D12CreateDevice hr=0x%08lx\n", (unsigned long)hr); if (FAILED(hr)) return 4;
    ffxConfigureDescGlobalDebug dbg{}; dbg.header.type = FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG; dbg.effectId = FFX_API_EFFECT_ID_FRAMEGENERATION; dbg.fpMessage = msg; dbg.debugLevel = FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_VERBOSE;
    printf("configure debug rc=%u\n", fn.Configure(nullptr, &dbg.header));
    uint64_t count = 0; ffxQueryDescGetVersions q{}; q.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS; q.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION; q.device = dev; q.outputCount = &count;
    ffxReturnCode_t rc = fn.Query(nullptr, &q.header); printf("GET_VERSIONS(count) rc=%u count=%llu\n", rc, (unsigned long long)count);
    std::vector<uint64_t> ids(count ? count : 1); std::vector<const char*> names(count ? count : 1);
    if (rc == 0 && count) { q.versionIds = ids.data(); q.versionNames = names.data(); rc = fn.Query(nullptr, &q.header); printf("GET_VERSIONS rc=%u\n", rc);
        for (uint64_t i = 0; i < count; ++i) printf("  provider[%llu] id=0x%llx name=%s\n", (unsigned long long)i, (unsigned long long)ids[i], names[i]); }
    int exitc = 0;
    for (uint64_t i = 0; i < (rc == 0 ? count : 0); ++i) {
        ffxCreateBackendDX12Desc be{}; be.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; be.device = dev;
        ffxCreateContextDescFrameGenerationVersion ver{}; ver.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION; ver.version = FFX_FRAMEGENERATION_VERSION;
        ffxOverrideVersion ov{}; ov.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION; ov.versionId = ids[i];
        ffxCreateContextDescFrameGeneration cd{}; cd.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
        cd.flags = FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED | FFX_FRAMEGENERATION_ENABLE_DEBUG_CHECKING;
        cd.displaySize = {1920, 1080}; cd.maxRenderSize = {1920, 1080}; cd.backBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
        cd.header.pNext = &ver.header; ver.header.pNext = &be.header; be.header.pNext = &ov.header;
        ffxContext ctx = nullptr; rc = fn.CreateContext(&ctx, &cd.header, nullptr); printf("CreateContext(FG provider %s) rc=%u ctx=%p\n", names[i], rc, ctx);
        if (rc == 0) {
            ffxQueryGetProviderVersion pv{}; pv.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION; rc = fn.Query(&ctx, &pv.header);
            printf("  active provider rc=%u id=0x%llx name=%s\n", rc, (unsigned long long)pv.versionId, pv.versionName ? pv.versionName : "(null)");
            FfxApiEffectMemoryUsage mu{}; ffxQueryDescFrameGenerationGetGPUMemoryUsage mq{}; mq.header.type = FFX_API_QUERY_DESC_TYPE_FRAMEGENERATION_GPU_MEMORY_USAGE; mq.gpuMemoryUsageFrameGeneration = &mu;
            rc = fn.Query(&ctx, &mq.header); printf("  GPU memory usage rc=%u total=%llu MiB aliasable=%llu MiB\n", rc, (unsigned long long)(mu.totalUsageInBytes >> 20), (unsigned long long)(mu.aliasableUsageInBytes >> 20));
            fn.DestroyContext(&ctx, nullptr);
        } else exitc = 5;
    }
    printf("DONE exit=%d\n", exitc); return exitc;
}

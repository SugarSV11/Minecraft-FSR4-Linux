// Experiment E1: does the OFFICIAL FSR SDK 2.3.0 upscaler DLL (4.1.1) load and create a
// context on a D3D12 device? Built as a Windows PE with mingw and run under Wine/VKD3D-Proton.
// This is a probe only: it performs NO dispatch, so it proves "context creation", not "upscaling".
#define WIN32_LEAN_AND_MEAN
#define _WINDOWS
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <cstdio>
#include <vector>
#include <ffx_api.h>
#include <ffx_upscale.h>
#include <dx12/ffx_api_dx12.h>
#include <ffx_api_loader.h>

static void msg(uint32_t t, const wchar_t* m) { wprintf(L"[ffx msg type=%u] %ls\n", t, m); fflush(stdout); }

int main(int argc, char** argv) {
    if (argc > 2) freopen(argv[2], "w", stdout);  // Proton does not forward console stdout
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char* dll = argc > 1 ? argv[1] : "amd_fidelityfx_loader_dx12.dll";
    printf("loading %s\n", dll);
    HMODULE ffx = LoadLibraryA(dll);
    if (!ffx) { printf("FAIL LoadLibrary err=%lu\n", GetLastError()); return 2; }
    ffxFunctions fn{}; ffxLoadFunctions(&fn, ffx);
    printf("entry points: create=%p query=%p dispatch=%p\n", (void*)fn.CreateContext, (void*)fn.Query, (void*)fn.Dispatch);
    if (!fn.CreateContext || !fn.Query) return 3;

    ID3D12Device* dev = nullptr;
    HRESULT hr = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), (void**)&dev);
    printf("D3D12CreateDevice hr=0x%08lx dev=%p\n", (unsigned long)hr, (void*)dev);
    if (FAILED(hr)) return 4;
    D3D12_FEATURE_DATA_SHADER_MODEL sm{D3D_SHADER_MODEL_6_6};
    hr = dev->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm));
    printf("SM6.6 query hr=0x%08lx highest=0x%x\n", (unsigned long)hr, (unsigned)sm.HighestShaderModel);

    ffxConfigureDescGlobalDebug dbg{}; dbg.header.type = FFX_API_CONFIGURE_DESC_TYPE_GLOBALDEBUG;
    dbg.effectId = FFX_API_EFFECT_ID_UPSCALE; dbg.fpMessage = msg; dbg.debugLevel = FFX_API_CONFIGURE_GLOBALDEBUG_LEVEL_VERBOSE;
    printf("configure debug rc=%u\n", fn.Configure(nullptr, &dbg.header));

    ffxCreateBackendDX12Desc be{}; be.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; be.device = dev;
    uint64_t count = 0;
    ffxQueryDescGetVersions q{}; q.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    q.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE; q.device = dev; q.outputCount = &count;
    ffxReturnCode_t rc = fn.Query(nullptr, &q.header);
    printf("GET_VERSIONS(count) rc=%u count=%llu\n", rc, (unsigned long long)count);
    std::vector<uint64_t> ids(count ? count : 1); std::vector<const char*> names(count ? count : 1);
    if (rc == 0 && count) {
        q.versionIds = ids.data(); q.versionNames = names.data();
        rc = fn.Query(nullptr, &q.header);
        printf("GET_VERSIONS rc=%u\n", rc);
        for (uint64_t i = 0; i < count; ++i) printf("  provider[%llu] id=0x%llx name=%s\n", (unsigned long long)i, (unsigned long long)ids[i], names[i]);
    }
    int exitc = 0;
    for (uint64_t i = 0; i < (rc == 0 ? count : 0); ++i) {
        ffxCreateContextDescUpscale cd{}; cd.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;
        cd.flags = FFX_UPSCALE_ENABLE_DEPTH_INVERTED | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE | FFX_UPSCALE_ENABLE_DEBUG_CHECKING;
        cd.maxRenderSize = {1280, 720}; cd.maxUpscaleSize = {1920, 1080}; cd.fpMessage = msg;
        ffxOverrideVersion ov{}; ov.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION; ov.versionId = ids[i];
        cd.header.pNext = &be.header; be.header.pNext = &ov.header;
        ffxContext ctx = nullptr;
        rc = fn.CreateContext(&ctx, &cd.header, nullptr);
        printf("CreateContext(provider %s) rc=%u ctx=%p\n", names[i], rc, ctx);
        if (rc == 0) {
            ffxQueryGetProviderVersion pv{}; pv.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
            rc = fn.Query(&ctx, &pv.header);
            printf("  active provider rc=%u id=0x%llx name=%s\n", rc, (unsigned long long)pv.versionId, pv.versionName ? pv.versionName : "(null)");
            fn.DestroyContext(&ctx, nullptr);
        } else exitc = 5;
    }
    printf("DONE exit=%d\n", exitc);
    return exitc;
}

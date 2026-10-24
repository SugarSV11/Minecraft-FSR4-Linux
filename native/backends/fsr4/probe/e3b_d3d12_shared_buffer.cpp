// E3b (Windows side): create a SHARED D3D12 buffer in VRAM, then wait for the native Linux process to write a
// pattern into it through Vulkan (imported dma-buf). Finally read the buffer back with D3D12 and verify.
// usage: e3b.exe <sync file (Windows path, 4096 bytes, pre-created)> <log file>
// sync words: [0]=helper stage (1=buffer created, 3=verified-ok, 4=verified-FAIL) [1]=native stage (2=pattern written)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <cstdint>
#include <vector>
#define CK(x) do { HRESULT h_ = (x); if (FAILED(h_)) { printf("FAIL %s hr=0x%08lx\n", #x, (unsigned long)h_); fflush(stdout); return 10; } } while (0)
int main(int argc, char** argv) {
    freopen(argv[2], "w", stdout); setvbuf(stdout, nullptr, _IONBF, 0);
    HANDLE f = CreateFileA(argv[1], GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    HANDLE m = CreateFileMappingA(f, nullptr, PAGE_READWRITE, 0, 0, nullptr);
    volatile uint32_t* sync = (volatile uint32_t*)MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    ID3D12Device* dev; CK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), (void**)&dev));
    const UINT64 N = 16u << 20;   // 2048 x 2048 x 4 bytes
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    ID3D12Resource* shared = nullptr; HANDLE sh = nullptr;
    struct V { D3D12_TEXTURE_LAYOUT lay; D3D12_RESOURCE_FLAGS fl; DWORD access; const char* n; } variants[] = {
        {D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE, 0, "row-major, access 0"},
        {D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS, 0, "row-major simultaneous, access 0"},
        {D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS, 0, "unknown layout simultaneous, access 0"},
        {D3D12_TEXTURE_LAYOUT_UNKNOWN, D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS, 0x80000001u, "unknown layout simultaneous, DXGI_SHARED_RESOURCE_READ|WRITE"},
    };
    for (auto& v : variants) {
        D3D12_RESOURCE_DESC rd{D3D12_RESOURCE_DIMENSION_TEXTURE2D, 0, 2048, 2048, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, v.lay, v.fl};
        ID3D12Resource* r = nullptr; HRESULT hr = dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_SHARED, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, __uuidof(ID3D12Resource), (void**)&r);
        printf("variant '%s': create hr=0x%08lx", v.n, (unsigned long)hr); if (FAILED(hr)) { printf("\n"); continue; }
        HANDLE h = nullptr; hr = dev->CreateSharedHandle(r, nullptr, v.access, nullptr, &h); printf(" CreateSharedHandle hr=0x%08lx handle=%p\n", (unsigned long)hr, h);
        if (SUCCEEDED(hr) && h && !shared) { shared = r; sh = h; printf("  -> using this variant\n"); }
    }
    if (!shared) { printf("no variant produced a shareable resource\n"); sync[0] = 5; return 13; }
    printf("shared texture created, NT handle=%p size=%llu\n", sh, (unsigned long long)N);
    sync[2] = (uint32_t)(N >> 10);   // size in KiB for the native side to match against dma-buf size
    sync[0] = 1;
    D3D12_HEAP_PROPERTIES rp{D3D12_HEAP_TYPE_READBACK};
    D3D12_RESOURCE_DESC bd{D3D12_RESOURCE_DIMENSION_BUFFER, 0, N, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
    ID3D12Resource* rb; CK(dev->CreateCommittedResource(&rp, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), (void**)&rb));
    D3D12_COMMAND_QUEUE_DESC qd{D3D12_COMMAND_LIST_TYPE_DIRECT}; ID3D12CommandQueue* q; CK(dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&q));
    ID3D12CommandAllocator* al; CK(dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&al));
    ID3D12GraphicsCommandList* cl; CK(dev->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, al, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&cl)); cl->Close();
    ID3D12Fence* fe; CK(dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&fe)); HANDLE ev = CreateEventA(nullptr, FALSE, FALSE, nullptr); UINT64 fv = 0;
    uint32_t seen = 0;
    for (int spins = 0; spins < 60000; ++spins) {
        uint32_t cmd = sync[1]; if (cmd == seen || cmd == 0) { Sleep(10); continue; } if (cmd == 0xFFFF) break; seen = cmd;
        al->Reset(); cl->Reset(al, nullptr);
        D3D12_TEXTURE_COPY_LOCATION dl{}, sl{}; dl.pResource = rb; dl.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dl.PlacedFootprint = {0, {DXGI_FORMAT_R8G8B8A8_UNORM, 2048, 2048, 1, 8192}}; sl.pResource = shared; sl.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        cl->CopyTextureRegion(&dl, 0, 0, 0, &sl, nullptr); cl->Close(); ID3D12CommandList* ls[] = {cl}; q->ExecuteCommandLists(1, ls); q->Signal(fe, ++fv); fe->SetEventOnCompletion(fv, ev); WaitForSingleObject(ev, 10000);
        uint32_t* p; D3D12_RANGE rr{0, (SIZE_T)N}; CK(rb->Map(0, &rr, (void**)&p));
        size_t bad = 0, first = (size_t)-1; uint64_t sum = 0, sumRef = 0; uint32_t x = 0, xRef = 0, nz = 0;
        for (size_t i = 0; i < N / 4; ++i) { uint32_t ref = 0xA5000000u ^ (uint32_t)i * 2654435761u; if (p[i] != ref) { if (first == (size_t)-1) first = i; ++bad; } sum += p[i]; sumRef += ref; x ^= p[i]; xRef ^= ref; nz += p[i] != 0; }
        printf("round %u: %zu / %zu words differ from logical pattern (first bad %lld); multiset check: sum %s xor %s, nonzero words %u\n", seen, bad, (size_t)(N / 4), (long long)first, sum == sumRef ? "EQUAL" : "differs", x == xRef ? "EQUAL" : "differs", nz);
        rb->Unmap(0, nullptr);
        sync[3] = seen; sync[0] = bad ? 4 : 3;
    }
    return 0;
}

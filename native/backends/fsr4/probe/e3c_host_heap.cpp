// E3c: which memory does VKD3D-Proton accept for ID3D12Device3::OpenExistingHeapFromAddress?
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <cstdint>
int main(int argc, char** argv) {
    freopen(argv[2], "w", stdout); setvbuf(stdout, nullptr, _IONBF, 0);
    ID3D12Device* dev; if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), (void**)&dev))) return 1;
    ID3D12Device3* d3; HRESULT hr = dev->QueryInterface(__uuidof(ID3D12Device3), (void**)&d3); printf("QI Device3 hr=0x%08lx\n", (unsigned long)hr);
    auto tryAddr = [&](const char* what, void* p) { ID3D12Heap* h = nullptr; HRESULT r = d3->OpenExistingHeapFromAddress(p, __uuidof(ID3D12Heap), (void**)&h);
        printf("%-48s addr=%p hr=0x%08lx", what, p, (unsigned long)r); if (SUCCEEDED(r)) { D3D12_HEAP_DESC d = h->GetDesc(); printf(" heapSize=%llu type=%d cpuPage=%d pool=%d", (unsigned long long)d.SizeInBytes, d.Properties.Type, d.Properties.CPUPageProperty, d.Properties.MemoryPoolPreference); h->Release(); } printf("\n"); };
    void* va = VirtualAlloc(nullptr, 1 << 20, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    tryAddr("VirtualAlloc 1MB (private)", va);
    tryAddr("VirtualAlloc + 4096", (char*)va + 4096);
    HANDLE f = CreateFileA(argv[1], GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    HANDLE m = CreateFileMappingA(f, nullptr, PAGE_READWRITE, 0, 0, nullptr); char* v = (char*)MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    tryAddr("MapViewOfFile(file in /dev/shm) base", v); tryAddr("MapViewOfFile + 4096", v + 4096); tryAddr("MapViewOfFile + 65536", v + 65536);
    HANDLE pm = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, 1 << 20, nullptr); char* pv = (char*)MapViewOfFile(pm, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    tryAddr("MapViewOfFile(pagefile-backed) base", pv);
    {   ID3D12Heap* h = nullptr; d3->OpenExistingHeapFromAddress(v, __uuidof(ID3D12Heap), (void**)&h);
        D3D12_HEAP_DESC hd = h->GetDesc(); printf("heap: size=%llu align=%llu flags=0x%x\n", (unsigned long long)hd.SizeInBytes, (unsigned long long)hd.Alignment, hd.Flags);
        struct T { UINT64 off, size; D3D12_RESOURCE_STATES st; const char* n; } tests[] = {
            {0, 65536, D3D12_RESOURCE_STATE_COPY_DEST, "off0 64K COPY_DEST"}, {0, 65536, D3D12_RESOURCE_STATE_GENERIC_READ, "off0 64K GENERIC_READ"}, {0, 65536, D3D12_RESOURCE_STATE_COMMON, "off0 64K COMMON"},
            {0, 4096, D3D12_RESOURCE_STATE_COPY_DEST, "off0 4K COPY_DEST"}, {65536, 65536, D3D12_RESOURCE_STATE_COPY_DEST, "off64K 64K COPY_DEST"}, {65536, 4096, D3D12_RESOURCE_STATE_COPY_DEST, "off64K 4K COPY_DEST"},
            {0, 1 << 19, D3D12_RESOURCE_STATE_COPY_DEST, "off0 512K COPY_DEST"}, {0, 1 << 20, D3D12_RESOURCE_STATE_COPY_DEST, "off0 whole(1M) COPY_DEST"}, {262144, 262144, D3D12_RESOURCE_STATE_COPY_DEST, "off256K 256K COPY_DEST"} };
        for (auto& t : tests) { D3D12_RESOURCE_DESC bd{D3D12_RESOURCE_DIMENSION_BUFFER, 0, t.size, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE};
            ID3D12Resource* r = nullptr; HRESULT hr = dev->CreatePlacedResource(h, t.off, &bd, t.st, nullptr, __uuidof(ID3D12Resource), (void**)&r); printf("placed %-28s hr=0x%08lx\n", t.n, (unsigned long)hr); if (r) r->Release(); } }
    MEMORY_BASIC_INFORMATION mbi; VirtualQuery(v, &mbi, sizeof mbi); printf("VirtualQuery(file view): base=%p size=%zu type=0x%lx state=0x%lx prot=0x%lx\n", mbi.BaseAddress, (size_t)mbi.RegionSize, mbi.Type, mbi.State, mbi.Protect);
    return 0;
}

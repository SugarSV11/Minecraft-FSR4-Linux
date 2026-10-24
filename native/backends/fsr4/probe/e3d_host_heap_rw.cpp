// E3d: is data really shared in both directions through a D3D12 heap opened over a MapViewOfFile region?
// A) CPU writes pattern into the mapped file -> GPU copies host buffer -> readback buffer -> CPU verifies
// B) GPU fills a default buffer with pattern (via upload) -> copies into host buffer -> CPU reads the mapped file
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#define CK(x) do { HRESULT h_ = (x); if (FAILED(h_)) { printf("FAIL %s hr=0x%08lx line %d\n", #x, (unsigned long)h_, __LINE__); return 10; } } while (0)
struct G { ID3D12Device* d; ID3D12CommandQueue* q; ID3D12CommandAllocator* a; ID3D12GraphicsCommandList* l; ID3D12Fence* f; HANDLE e; UINT64 v = 0; };
static ID3D12Resource* buf(G& g, UINT64 n, D3D12_HEAP_TYPE t, D3D12_RESOURCE_STATES st) { D3D12_HEAP_PROPERTIES hp{t}; D3D12_RESOURCE_DESC rd{D3D12_RESOURCE_DIMENSION_BUFFER, 0, n, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_NONE}; ID3D12Resource* r = nullptr; g.d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, st, nullptr, __uuidof(ID3D12Resource), (void**)&r); return r; }
static void run(G& g) { g.l->Close(); ID3D12CommandList* ls[] = {g.l}; g.q->ExecuteCommandLists(1, ls); g.q->Signal(g.f, ++g.v); g.f->SetEventOnCompletion(g.v, g.e); WaitForSingleObject(g.e, 10000); g.a->Reset(); g.l->Reset(g.a, nullptr); }
static void bar(G& g, ID3D12Resource* r, D3D12_RESOURCE_STATES a, D3D12_RESOURCE_STATES b) { D3D12_RESOURCE_BARRIER br{}; br.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; br.Transition = {r, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, a, b}; g.l->ResourceBarrier(1, &br); }
int main(int argc, char** argv) {
    freopen(argv[2], "w", stdout); setvbuf(stdout, nullptr, _IONBF, 0);
    G g{}; CK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), (void**)&g.d));
    D3D12_COMMAND_QUEUE_DESC qd{D3D12_COMMAND_LIST_TYPE_DIRECT}; CK(g.d->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&g.q));
    CK(g.d->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&g.a)); CK(g.d->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g.a, nullptr, __uuidof(ID3D12GraphicsCommandList), (void**)&g.l));
    CK(g.d->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&g.f)); g.e = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    HANDLE f = CreateFileA(argv[1], GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    HANDLE m = CreateFileMappingA(f, nullptr, PAGE_READWRITE, 0, 0, nullptr); uint8_t* v = (uint8_t*)MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    ID3D12Device3* d3; CK(g.d->QueryInterface(__uuidof(ID3D12Device3), (void**)&d3)); ID3D12Heap* h; CK(d3->OpenExistingHeapFromAddress(v, __uuidof(ID3D12Heap), (void**)&h));
    const UINT64 N = 4 << 20, OFF = 65536;
    D3D12_RESOURCE_DESC bd{D3D12_RESOURCE_DIMENSION_BUFFER, 0, N, 1, 1, 1, DXGI_FORMAT_UNKNOWN, {1, 0}, D3D12_TEXTURE_LAYOUT_ROW_MAJOR, D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER};
    ID3D12Resource* hb; CK(g.d->CreatePlacedResource(h, OFF, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource), (void**)&hb));
    uint32_t* cpu = (uint32_t*)(v + OFF);
    // A) CPU -> GPU
    for (size_t i = 0; i < N / 4; ++i) cpu[i] = 0xC0000000u + (uint32_t)i;
    ID3D12Resource* rb = buf(g, N, D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST);
    bar(g, hb, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE); g.l->CopyBufferRegion(rb, 0, hb, 0, N); bar(g, hb, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST); run(g);
    uint32_t* p; D3D12_RANGE rr{0, (SIZE_T)N}; rb->Map(0, &rr, (void**)&p); size_t bad = 0; for (size_t i = 0; i < N / 4; ++i) bad += p[i] != 0xC0000000u + (uint32_t)i; printf("A) CPU->GPU through host heap: %zu bad of %zu words (first word GPU saw: 0x%08x)\n", bad, (size_t)(N / 4), p[0]);
    // B) GPU -> CPU
    memset(cpu, 0, N);
    ID3D12Resource* up = buf(g, N, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ); uint32_t* u; up->Map(0, nullptr, (void**)&u); for (size_t i = 0; i < N / 4; ++i) u[i] = 0xD0000000u + (uint32_t)i;
    g.l->CopyBufferRegion(hb, 0, up, 0, N); run(g);
    bad = 0; for (size_t i = 0; i < N / 4; ++i) bad += cpu[i] != 0xD0000000u + (uint32_t)i; printf("B) GPU->CPU through host heap: %zu bad of %zu words (first word CPU sees: 0x%08x)\n", bad, (size_t)(N / 4), cpu[0]);
    return 0;
}

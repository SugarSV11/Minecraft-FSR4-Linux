// E4 (Windows side): create a SHARED D3D12 fence and a command queue; cooperate with the native Linux process.
// sync words: [0]=helper stage (1 = fence created) [1]=native command (5 = native signalled value 5, 0xFFFF quit) [2]=value helper reached [3]=helper result
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <cstdint>
#define CK(x) do { HRESULT h_ = (x); if (FAILED(h_)) { printf("FAIL %s hr=0x%08lx\n", #x, (unsigned long)h_); fflush(stdout); return 10; } } while (0)
int main(int argc, char** argv) {
    freopen(argv[2], "w", stdout); setvbuf(stdout, nullptr, _IONBF, 0);
    HANDLE f = CreateFileA(argv[1], GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    HANDLE m = CreateFileMappingA(f, nullptr, PAGE_READWRITE, 0, 0, nullptr);
    volatile uint32_t* sync = (volatile uint32_t*)MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    ID3D12Device* dev; CK(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), (void**)&dev));
    D3D12_COMMAND_QUEUE_DESC qd{D3D12_COMMAND_LIST_TYPE_DIRECT}; ID3D12CommandQueue* q; CK(dev->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&q));
    sync[0] = 100;   // device ready: native snapshots its fd table now
    for (int i = 0; i < 3000 && sync[1] != 1; ++i) Sleep(10);
    ID3D12Fence* fe; CK(dev->CreateFence(0, D3D12_FENCE_FLAG_SHARED, __uuidof(ID3D12Fence), (void**)&fe));
    HANDLE sh = nullptr; HRESULT hr = dev->CreateSharedHandle(fe, nullptr, GENERIC_ALL, nullptr, &sh);
    printf("shared fence: CreateSharedHandle hr=0x%08lx handle=%p\n", (unsigned long)hr, sh);
    if (FAILED(hr)) { sync[0] = 5; return 11; }
    sync[0] = 1;
    // native -> D3D12: wait until the fence reaches 5 (signalled by native Vulkan)
    for (int i = 0; i < 1000; ++i) { if (sync[1] == 5) break; Sleep(10); }
    for (int i = 0; i < 500; ++i) { if (fe->GetCompletedValue() >= 5) break; Sleep(10); }
    UINT64 v = fe->GetCompletedValue(); printf("D3D12 sees completed value %llu after native signalled 5\n", (unsigned long long)v); sync[2] = (uint32_t)v;
    // GPU-queue wait on value 5 then signal 7 (queue-side dependency chain): q->Wait(fe,5); q->Signal(fe,7)
    CK(q->Wait(fe, 5)); CK(q->Signal(fe, 7));
    for (int i = 0; i < 500 && fe->GetCompletedValue() < 7; ++i) Sleep(10);
    printf("after q->Wait(5)+Signal(7): completed=%llu\n", (unsigned long long)fe->GetCompletedValue());
    sync[3] = fe->GetCompletedValue() >= 7 ? 7 : 1;
    // wait-before-signal: queue waits for 20 (not yet signalled), native will signal 20 later; then queue signals 21
    CK(q->Wait(fe, 20)); CK(q->Signal(fe, 21)); sync[0] = 2;
    for (int i = 0; i < 1000 && sync[1] != 0xFFFF; ++i) Sleep(10);
    printf("final completed=%llu\n", (unsigned long long)fe->GetCompletedValue());
    return 0;
}

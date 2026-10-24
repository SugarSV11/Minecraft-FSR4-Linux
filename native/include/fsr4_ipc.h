// fsr4_ipc.h - versioned shared-memory protocol between the Linux side (Minecraft / JNI lib / test client)
// and the Windows helper (fsr4_bridge.exe) running under Wine/GE-Proton. Pure C++17, no OS headers, so the
// exact same file is compiled by g++ (Linux) and x86_64-w64-mingw32-g++ (Windows PE).
//
// Memory model: one mapped file (/dev/shm/<name> == Z:\dev\shm\<name>). All control words are accessed with
// acquire/release atomics. Single producer (Linux) / single consumer (helper) per direction; one request in
// flight at a time (request/response handshake by sequence numbers). Data planes are plain bytes.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace fsr4ipc {

constexpr uint32_t kMagic = 0x34525346u;  // 'FSR4'
constexpr uint32_t kVersion = 3;

enum class Fmt : uint32_t { None = 0, RGBA16F = 1, R32F = 2, RG16F = 3, RGBA8 = 4 };
constexpr uint32_t bytesPerPixel(Fmt f) {
    return f == Fmt::RGBA16F ? 8 : f == Fmt::R32F ? 4 : f == Fmt::RG16F ? 4 : f == Fmt::RGBA8 ? 4 : 0;
}

enum HelperState : uint32_t { kStarting = 0, kReady = 1, kFailed = 2, kShutdown = 3 };

// Request flags
enum ReqFlags : uint32_t {
    kReset = 1u << 0,           // discard temporal history (camera cut)
    kDepthInverted = 1u << 1,   // depth buffer holds reversed-Z (1 near .. 0 far)
    kHdr = 1u << 2,
    kAutoExposure = 1u << 3,
    kNonLinearSrgb = 1u << 4,
    kDepthInfinite = 1u << 5,
};

// Response status
enum Cmd : uint32_t { kCmdFrame = 0, kCmdAlloc = 1, kCmdFree = 2, kCmdSelfTest = 3, kCmdFenceInit = 4, kCmdFenceTest = 5,
           kCmdFgAlloc = 6, kCmdFgFree = 7, kCmdFgFrame = 8 };   // frame generation (FSR FG API 4.0.1): needs the 4 base planes; adds plane 4 (final colour, display size) and 5 (generated frame, UAV)

enum Status : uint32_t {
    kOk = 0, kBadRequest = 1, kContextFailed = 2, kDispatchFailed = 3, kProviderMismatch = 4, kInternal = 5,
};

struct alignas(64) Header {
    uint32_t magic, version;
    uint64_t totalSize;          // size of the mapping, written by the creator
    uint32_t maxRenderW, maxRenderH, maxOutW, maxOutH;
    uint64_t colorOff, depthOff, mvOff, outOff;   // plane offsets from mapping start (set by creator)
    uint64_t colorCap, depthCap, mvCap, outCap;   // plane capacities in bytes
    char requestedProvider[32];  // e.g. "4.1.1" / "3.1.5"; creator writes before starting the helper
    // --- helper -> client ---
    std::atomic<uint64_t> clientBeat;            // client -> helper lease: helper exits if this stops changing
    uint32_t transferMode;                       // 0 = helper memcpy + upload heaps, 1 = D3D12 heaps opened on the shared pages (GPU copies, no CPU copies)
    uint32_t leaseTimeoutMs;                     // 0 = no lease
    std::atomic<uint32_t> helperState;
    uint32_t helperPid;
    std::atomic<uint64_t> heartbeat;             // incremented by helper every loop iteration
    char offered[160];           // space separated provider names offered by the AMD runtime for this device (e.g. "4.1.1 3.1.5 2.3.4")
    char activeProvider[32];     // provider the FFX runtime reports as ACTUALLY running (never assumed)
    uint64_t activeProviderId;
    char error[256];
};

struct alignas(64) Request {
    std::atomic<uint64_t> seq;   // client increments last, with release; helper acquires
    uint32_t cmd;                // Cmd; kCmdAlloc/kCmdFree/kCmdSelfTest are only valid in transferMode 2 (shared GPU textures)
    uint32_t pad0;
    uint64_t frameId;
    uint32_t renderW, renderH, outW, outH;
    uint32_t flags;
    uint32_t colorFmt, depthFmt, mvFmt, outFmt;
    uint32_t colorPitch, depthPitch, mvPitch;    // bytes per row in the planes (>= w*bpp)
    float jitterX, jitterY;      // pixels at render resolution, as returned by ffxQuery jitter offset
    float mvScaleX, mvScaleY;
    float frameTimeMs, preExposure, sharpness, cameraNear, cameraFar, fovY;
    uint32_t enableSharpening;
    uint32_t colorCrc, depthCrc, mvCrc;          // 0 = not provided; otherwise helper verifies integrity
    uint32_t quality;            // informational (FfxApiUpscaleQualityMode)
    char provider[16];           // provider for this request ("" = the one the helper was started with); a change recreates the FFX context
    float fgCam[12];             // kCmdFgFrame: camera position, up, right, forward (world space)
    uint64_t fenceWait, fenceSignal;   // kCmdFrame/kCmdFenceTest, transferMode 2 + shared fence: != 0 -> asynchronous frame: the D3D12 queue waits for fenceWait (signalled by native Vulkan after copy-in) and signals fenceSignal after copy-out; the helper does NOT wait on the CPU
};

struct alignas(64) Response {
    std::atomic<uint64_t> seq;   // == request seq when done
    uint64_t frameId;
    uint32_t status;
    uint32_t ffxReturnCode;
    uint32_t outPitch;
    uint32_t outCrc;
    uint32_t sharedCount;        // number of shared textures created by kCmdAlloc (4: color, depth, mv, out)
    uint32_t sharedCrc[7];       // kCmdSelfTest: CRC32 of the logical (tight) rows of each shared texture as D3D12 sees it
    float gpuDispatchMs;         // GPU timestamps around ffxDispatch only
    float helperTotalMs;         // from request seen to response published
    float helperUploadMs, helperReadbackMs;
    char message[128];
};

struct Layout {
    Header hdr; Request req; Response resp;
};

// Both the Linux library and MinGW helper must agree on the mapped ABI.
static_assert(sizeof(Header) == 640 && alignof(Header) == 64);
static_assert(sizeof(Request) == 256 && alignof(Request) == 64);
static_assert(sizeof(Response) == 256 && alignof(Response) == 64);
static_assert(sizeof(Layout) == 1152 && offsetof(Layout, resp) == 896);

constexpr size_t alignUp(size_t v, size_t a) { return (v + a - 1) / a * a; }
constexpr size_t kPlaneAlign = 65536;   // planes start on 64 KiB boundaries: needed for D3D12 placed resources inside one host heap

// Compute plane layout for given maxima. Returns total size.
inline size_t computeLayout(Header& h, uint32_t maxRW, uint32_t maxRH, uint32_t maxOW, uint32_t maxOH) {
    h.maxRenderW = maxRW; h.maxRenderH = maxRH; h.maxOutW = maxOW; h.maxOutH = maxOH;
    size_t off = alignUp(sizeof(Layout), kPlaneAlign);
    auto take = [&](uint64_t& o, uint64_t& cap, size_t bytes) { o = off; cap = alignUp(bytes, kPlaneAlign); off += cap; };
    take(h.colorOff, h.colorCap, (size_t)maxRW * maxRH * 8);
    take(h.depthOff, h.depthCap, (size_t)maxRW * maxRH * 4);
    take(h.mvOff, h.mvCap, (size_t)maxRW * maxRH * 4);
    take(h.outOff, h.outCap, (size_t)maxOW * maxOH * 8);
    h.totalSize = off;
    return off;
}

inline uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc = 0) {
    static const auto table = [] {
        struct T { uint32_t v[256]; } t;
        for (uint32_t i = 0; i < 256; ++i) { uint32_t c = i; for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); t.v[i] = c; }
        return t;
    }();
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) crc = (crc >> 8) ^ table.v[(crc ^ p[i]) & 0xff];
    return ~crc;
}

}  // namespace fsr4ipc

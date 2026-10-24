// Linux-side client of the FSR bridge (see fsr4_ipc.h). Owns the shared mapping and the Wine helper process.
// Thread-safety: one thread submits at a time (the render thread, or a worker owned by the caller).
#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <sys/types.h>
#include "fsr4_ipc.h"

namespace fsr4 {

struct BridgeConfig {
    std::string protonDir;      // e.g. ~/.steam/steam/compatibilitytools.d/GE-Proton11-6-x86_64
    std::string prefixDir;      // STEAM_COMPAT_DATA_PATH (disposable prefix)
    std::string steamDir;       // STEAM_COMPAT_CLIENT_INSTALL_PATH
    std::string helperExe;      // fsr4_bridge.exe (Linux path, directory must also hold the AMD DLLs)
    std::string logFile;        // helper log (Linux path)
    std::string provider;       // "4.1.1" or "3.1.5"
    uint32_t maxRenderW = 1920, maxRenderH = 1080, maxOutW = 1920, maxOutH = 1080;
    int startTimeoutMs = 90000;
    int transferMode = 0;        // 0 = helper copies through upload/readback heaps, 1 = shared pages opened as D3D12 heaps
    int leaseTimeoutMs = 4000;   // helper exits by itself if the client stops renewing the lease
};

enum class BridgeResult { Ok, NotRunning, Timeout, HelperDied, HelperStalled, Rejected, Internal };

struct FrameParams {   // everything the helper needs besides the planes (planes are written by the caller)
    std::string provider;   // "" = helper default; otherwise the provider for this request
    uint32_t cmd = 0;   // fsr4ipc::Cmd
    uint64_t frameId = 0; uint32_t renderW = 0, renderH = 0, outW = 0, outH = 0, flags = 0, quality = 1;
    float jitterX = 0, jitterY = 0, mvScaleX = 1, mvScaleY = 1, frameTimeMs = 16.6f, preExposure = 1, sharpness = 0, cameraNear = 0.1f, cameraFar = 1000, fovY = 1;
    bool enableSharpening = false, withCrc = false, corruptCrc = false;  // corruptCrc: test hook, sends a wrong CRC
    float fgCam[12] = {};   // kCmdFgFrame: camera position, up, right, forward
    uint64_t fenceWait = 0, fenceSignal = 0;   // != 0: asynchronous frame through the shared fence (see fsr4_ipc.h)
    uint32_t colorPitch = 0, depthPitch = 0, mvPitch = 0;   // 0 -> tightly packed
    uint32_t colorFmt = 1, depthFmt = 2, mvFmt = 3, outFmt = 1;   // fsr4ipc::Fmt values (overridable for negative tests)
};

class BridgeClient {
public:
    ~BridgeClient();
    // Creates the mapping and starts the helper; blocks until it reports Ready or failed. Returns false + error().
    bool start(const BridgeConfig& cfg);
    bool running();                       // helper alive and Ready
    uint8_t* colorPlane() { return base_ + hdr()->colorOff; }
    uint8_t* depthPlane() { return base_ + hdr()->depthOff; }
    uint8_t* mvPlane() { return base_ + hdr()->mvOff; }
    const uint8_t* outPlane() const { return base_ + hdr()->outOff; }
    // Publish a request and wait for the response (timeoutMs). Planes must be filled before.
    BridgeResult submit(FrameParams p, int timeoutMs, fsr4ipc::Response* outResp);
    // Split form (asynchronous requests): post() publishes the request and returns immediately; collect() must be called before the next post()/submit().
    BridgeResult post(FrameParams p);
    BridgeResult collect(int timeoutMs, fsr4ipc::Response* outResp);
    bool outstanding() const { return outstanding_; }
    void stop();                          // clean shutdown, then force-kill if needed
    void kill();                          // force-kill the helper (used by crash-recovery tests)
    pid_t childPid() const { return child_; }
    void debugStopLease() { leaseRun_ = false; if (lease_.joinable()) lease_.join(); }   // simulate a dead client (tests)
    std::vector<pid_t> helperLinuxPids() const;   // Wine helper + launcher processes (tests / diagnostics)

    const std::string& error() const { return err_; }
    std::string activeProvider() const;
    std::string offeredProviders() const { return base_ ? std::string(hdr()->offered) : ""; }
    uint64_t helperPid() const;
    fsr4ipc::Header* hdr() const { return reinterpret_cast<fsr4ipc::Header*>(base_); }
private:
    fsr4ipc::Request* req() const { return &reinterpret_cast<fsr4ipc::Layout*>(base_)->req; }
    fsr4ipc::Response* resp() const { return &reinterpret_cast<fsr4ipc::Layout*>(base_)->resp; }
    bool childAlive();
    bool startLocked(const BridgeConfig& cfg); void stopLocked();
    std::mutex lifeMu_; std::atomic<bool> abortStart_{false};   // stop() from another thread (game exit) while start() is still waiting for the helper: abort the wait instead of unmapping under it
    void killHelperProcesses();   // finds Wine processes by their unique shm path (they are re-parented, not our children)
    std::thread lease_; std::atomic<bool> leaseRun_{false};
    BridgeConfig cfg_; std::string path_, err_;
    bool outstanding_ = false; uint64_t pendingFrameId_ = 0;
    uint8_t* base_ = nullptr; size_t size_ = 0; int fd_ = -1; pid_t child_ = -1; uint64_t seq_ = 0;
};

}  // namespace fsr4

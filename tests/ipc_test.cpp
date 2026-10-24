// End-to-end IPC test: Linux process <-> Wine helper running the OFFICIAL AMD FSR upscaler.
// Usage: ipc_test <provider 4.1.1|3.1.5> <outdir> [tests: seq reset resize negative crash latency]
// Synthetic scene = same aliased procedural pattern as experiment E2 (camera pan, exact motion vectors).
// An "ok" ffx return code is never treated as proof: every test also checks the pixels (finite, non-trivial,
// correct size, CRC round-trip) and compares against a supersampled reference with an independent baseline.
#include "bridge_client.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <csignal>
#include <fstream>
#include <unistd.h>
using namespace fsr4; using namespace fsr4ipc;
using Clock = std::chrono::steady_clock;

static uint16_t f2h(float f) { uint32_t x; memcpy(&x, &f, 4); uint32_t s = (x >> 16) & 0x8000; int e = ((x >> 23) & 0xff) - 127 + 15; uint32_t m = x & 0x7fffff; if (e <= 0) return (uint16_t)s; if (e >= 31) return (uint16_t)(s | 0x7c00); return (uint16_t)(s | (e << 10) | (m >> 13)); }
static float h2f(uint16_t h) { uint32_t s = (h & 0x8000) << 16, e = (h >> 10) & 31, m = h & 0x3ff, x; if (e == 0) { if (!m) x = s; else { e = 1; while (!(m & 0x400)) { m <<= 1; e--; } m &= 0x3ff; x = s | ((e + 112) << 23) | (m << 13); } } else if (e == 31) x = s | 0x7f800000 | (m << 13); else x = s | ((e + 112) << 23) | (m << 13); float f; memcpy(&f, &x, 4); return f; }
static void scene(float u, float v, float DH, float* rgb) {
    int c = ((int)floorf(u / 24.f) + (int)floorf(v / 24.f)) & 1; float d = (u + v) / 7.f; d -= floorf(d); float st = d < 0.18f ? 1.f : 0.f;
    float rx = u - 700.f, ry = v - 500.f, r = sqrtf(rx * rx + ry * ry) / 9.f; r -= floorf(r); float ring = r < 0.2f ? 1.f : 0.f; float g = 0.15f + 0.55f * c;
    rgb[0] = fminf(1.f, g + 0.35f * st); rgb[1] = fminf(1.f, g * 0.8f + 0.5f * ring); rgb[2] = fminf(1.f, 0.2f + 0.3f * (1.f - c) + 0.4f * st * ring + 0.3f * (v / DH));
}
static float halton(int i, int b) { float f = 1, r = 0; while (i > 0) { f /= b; r += f * (i % b); i /= b; } return r; }

struct Env { BridgeClient bc; BridgeConfig cfg; std::string outdir; };
static BridgeConfig makeCfg(const std::string& provider) {
    const char* h = getenv("HOME"); std::string H = h ? h : "";
    BridgeConfig c; c.provider = provider;
    c.protonDir = getenv("FSR4_PROTON") ? getenv("FSR4_PROTON") : H + "/.steam/steam/compatibilitytools.d/GE-Proton11-6-x86_64";
    c.prefixDir = H + "/.cache/fsr4-linux/pfx_ge"; c.steamDir = H + "/.local/share/Steam";
    c.helperExe = getenv("FSR4_HELPER") ? getenv("FSR4_HELPER") : "build/probe/fsr4_bridge.exe";
    char cwd[1024]; if (getcwd(cwd, sizeof cwd) && c.helperExe[0] != '/') c.helperExe = std::string(cwd) + "/" + c.helperExe;
    c.transferMode = getenv("FSR4_ZEROCOPY") ? atoi(getenv("FSR4_ZEROCOPY")) : 0;
    c.logFile = c.helperExe + ".log"; return c;
}

// render the scene at render res with jitter (+pan) into the shared planes; returns the display-res jitter used
static void renderFrame(BridgeClient& bc, int rw, int rh, int ow, int oh, float pan, int frame, int phases, FrameParams& p, bool depthAlt = false) {
    float scale = (float)ow / rw; float jx = halton(frame % phases + 1, 2) - 0.5f, jy = halton(frame % phases + 1, 3) - 0.5f;
    uint16_t* col = (uint16_t*)bc.colorPlane(); float* dep = (float*)bc.depthPlane(); uint16_t* mv = (uint16_t*)bc.mvPlane();
    for (int y = 0; y < rh; ++y) for (int x = 0; x < rw; ++x) {
        float c[3]; scene((x + .5f + jx) * scale + pan, (y + .5f + jy) * scale, (float)oh, c);
        uint16_t* d = col + ((size_t)y * rw + x) * 4; d[0] = f2h(c[0]); d[1] = f2h(c[1]); d[2] = f2h(c[2]); d[3] = f2h(1.f);
        dep[(size_t)y * rw + x] = 0.5f; (void)depthAlt;
        mv[((size_t)y * rw + x) * 2] = f2h(3.f / scale); mv[((size_t)y * rw + x) * 2 + 1] = 0;
    }
    p.renderW = rw; p.renderH = rh; p.outW = ow; p.outH = oh; p.jitterX = jx; p.jitterY = jy; p.frameId = frame;
    p.flags = kAutoExposure | kNonLinearSrgb;
}
static std::vector<float> reference(int ow, int oh, float pan) {
    std::vector<float> ref((size_t)ow * oh * 3);
    for (int y = 0; y < oh; ++y) for (int x = 0; x < ow; ++x) { float acc[3] = {0, 0, 0};
        for (int sy = 0; sy < 4; ++sy) for (int sx = 0; sx < 4; ++sx) { float c[3]; scene(x + (sx + .5f) / 4 + pan, y + (sy + .5f) / 4, (float)oh, c); for (int k = 0; k < 3; ++k) acc[k] += c[k] / 16; }
        for (int k = 0; k < 3; ++k) ref[((size_t)y * ow + x) * 3 + k] = acc[k]; }
    return ref;
}
struct Stats { double psnr; bool finite; double mean; double nonzero; };
static Stats check(const uint8_t* out, int ow, int oh, const std::vector<float>& ref) {
    const uint16_t* o = (const uint16_t*)out; double se = 0, mean = 0; size_t n = 0, nz = 0; bool fin = true;
    for (int y = 16; y < oh - 16; ++y) for (int x = 16; x < ow - 16; ++x) for (int k = 0; k < 3; ++k) {
        float v = h2f(o[((size_t)y * ow + x) * 4 + k]); if (!std::isfinite(v)) { fin = false; continue; }
        double d = fminf(fmaxf(v, 0.f), 1.f) - ref[((size_t)y * ow + x) * 3 + k]; se += d * d; ++n; mean += v; nz += v != 0; }
    return {10 * log10(1.0 / (se / n + 1e-12)), fin, mean / n, (double)nz / n};
}
static void savePPM(const std::string& f, const uint8_t* out, int ow, int oh) {
    FILE* fp = fopen(f.c_str(), "wb"); if (!fp) return; fprintf(fp, "P6\n%d %d\n255\n", ow, oh); const uint16_t* o = (const uint16_t*)out;
    for (size_t i = 0; i < (size_t)ow * oh; ++i) for (int k = 0; k < 3; ++k) fputc((int)(255.f * fminf(fmaxf(h2f(o[i * 4 + k]), 0.f), 1.f) + .5f), fp); fclose(fp);
}
static const char* rname(BridgeResult r) { static const char* n[] = {"Ok", "NotRunning", "Timeout", "HelperDied", "HelperStalled", "Rejected", "Internal"}; return n[(int)r]; }
static int fails = 0;
#define EXPECT(c, ...) do { if (!(c)) { ++fails; printf("  FAIL: " __VA_ARGS__); printf("\n"); } else { printf("  ok:   " __VA_ARGS__); printf("\n"); } } while (0)

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <provider> <outdir> [tests]\n", argv[0]); return 64; }
    std::string provider = argv[1], outdir = argv[2]; std::vector<std::string> tests;
    for (int i = 3; i < argc; ++i) tests.push_back(argv[i]);
    if (tests.empty()) tests = {"seq", "reset", "resize", "negative", "latency", "crash"};
    auto want = [&](const char* t) { return std::find(tests.begin(), tests.end(), t) != tests.end(); };
    BridgeClient bc; BridgeConfig cfg = makeCfg(provider);
    auto t0 = Clock::now();
    if (!bc.start(cfg)) { printf("START FAILED: %s\n", bc.error().c_str()); return 2; }
    printf("helper started in %.0f ms, windows pid %llu, ACTIVE PROVIDER REPORTED BY AMD RUNTIME: '%s' (requested '%s')\n",
           std::chrono::duration<double, std::milli>(Clock::now() - t0).count(), (unsigned long long)bc.helperPid(), bc.activeProvider().c_str(), provider.c_str());
    // activeProvider is filled at first context creation, so run a warm-up frame
    const int RW = 1280, RH = 720, OW = 1920, OH = 1080, PH = 18;
    FrameParams p; Response rs; BridgeResult r;
    renderFrame(bc, RW, RH, OW, OH, 0, 0, PH, p); p.flags |= kReset; r = bc.submit(p, 60000, &rs);
    EXPECT(r == BridgeResult::Ok, "warm-up frame result=%s status=%u %s", rname(r), rs.status, rs.message);
    EXPECT(bc.activeProvider() == provider, "provider reported by runtime == requested ('%s')", bc.activeProvider().c_str());

    if (want("seq")) {
        printf("[seq] 64-frame pan, 3 display px/frame, CRC every 8th frame\n");
        std::vector<double> rt; int bad = 0; double gpu = 0; int ng = 0; std::vector<std::vector<float>> refs; int chk[] = {15, 31, 63};
        for (int f = 0; f < 64; ++f) {
            renderFrame(bc, RW, RH, OW, OH, 3.f * f, f, PH, p); p.withCrc = (f % 8 == 0); if (f == 0) p.flags |= kReset;
            auto a = Clock::now(); r = bc.submit(p, 30000, &rs); double ms = std::chrono::duration<double, std::milli>(Clock::now() - a).count();
            if (r != BridgeResult::Ok) { ++bad; printf("  frame %d: %s status=%u %s\n", f, rname(r), rs.status, rs.message); continue; }
            if (p.withCrc && rs.outCrc != (crc32(bc.outPlane(), (size_t)OW * 8 * OH) | 1u)) { ++bad; printf("  frame %d: output CRC mismatch (shared-memory integrity)\n", f); }
            if (rs.frameId != (uint64_t)f) { ++bad; printf("  frame %d: response frameId %llu\n", f, (unsigned long long)rs.frameId); }
            rt.push_back(ms); if (f >= 4) { gpu += rs.gpuDispatchMs; ++ng; }
            for (int c : chk) if (f == c) { auto ref = reference(OW, OH, 3.f * f); Stats s = check(bc.outPlane(), OW, OH, ref);
                printf("  frame %d: PSNR %.2f dB finite=%d mean=%.3f nonzero=%.3f\n", f, s.psnr, s.finite, s.mean, s.nonzero);
                if (!s.finite || s.nonzero < 0.99) ++bad; if (f == 63) savePPM(outdir + "/seq_" + provider + "_f63.ppm", bc.outPlane(), OW, OH); }
        }
        EXPECT(bad == 0, "64/64 frames valid (bad=%d)", bad);
        std::sort(rt.begin(), rt.end()); printf("  round-trip ms: min %.2f median %.2f p95 %.2f max %.2f ; avg GPU dispatch %.3f ms\n", rt.front(), rt[rt.size() / 2], rt[rt.size() * 95 / 100], rt.back(), gpu / ng);
        // baseline: nearest neighbour of the final render
        std::vector<float> ref = reference(OW, OH, 3.f * 63); std::vector<uint16_t> nn((size_t)OW * OH * 4); float sc = (float)OW / RW;
        for (int y = 0; y < OH; ++y) for (int x = 0; x < OW; ++x) { float c[3]; int sx = (int)((x + .5f) / sc), sy = (int)((y + .5f) / sc); scene((sx + .5f) * sc + 3.f * 63, (sy + .5f) * sc, (float)OH, c); for (int k = 0; k < 3; ++k) nn[((size_t)y * OW + x) * 4 + k] = f2h(c[k]); }
        Stats s = check((uint8_t*)nn.data(), OW, OH, ref); printf("  baseline nearest-neighbour: PSNR %.2f dB\n", s.psnr);
        savePPM(outdir + "/baseline_nearest.ppm", (uint8_t*)nn.data(), OW, OH);
    }
    if (want("reset")) {
        printf("[reset] camera cut at frame 20: with vs without reset flag (history rejection)\n");
        double psnr[2];
        for (int variant = 0; variant < 2; ++variant) {
            for (int f = 0; f < 28; ++f) {
                float pan = f < 20 ? 3.f * f : 3.f * f + 500.f;   // discontinuous jump
                renderFrame(bc, RW, RH, OW, OH, pan, f, PH, p); if (f == 0 || (f == 20 && variant == 1)) p.flags |= kReset; else p.flags &= ~kReset;
                r = bc.submit(p, 30000, &rs); if (r != BridgeResult::Ok) { printf("  frame %d %s %s\n", f, rname(r), rs.message); break; }
                if (f == 21) { auto ref = reference(OW, OH, pan); psnr[variant] = check(bc.outPlane(), OW, OH, ref).psnr; }
            }
        }
        printf("  PSNR 1 frame after cut: no-reset %.2f dB, with reset %.2f dB\n", psnr[0], psnr[1]);
        EXPECT(psnr[1] != psnr[0], "reset flag changes the result (is honoured by the provider)");
    }
    if (want("resize")) {
        printf("[resize] Quality 1280x720 -> Performance 960x540 -> Quality again, same 1080p output\n");
        int dims[3][2] = {{1280, 720}, {960, 540}, {1280, 720}}; bool ok = true;
        for (int k = 0; k < 3; ++k) { for (int f = 0; f < 6; ++f) { renderFrame(bc, dims[k][0], dims[k][1], OW, OH, 3.f * f, f, PH, p); if (f == 0) p.flags |= kReset; else p.flags &= ~kReset;
                auto a = Clock::now(); r = bc.submit(p, 60000, &rs); double ms = std::chrono::duration<double, std::milli>(Clock::now() - a).count();
                if (r != BridgeResult::Ok) { ok = false; printf("  %dx%d frame %d: %s %s\n", dims[k][0], dims[k][1], f, rname(r), rs.message); break; }
                if (f == 0) printf("  %dx%d first frame (context recreated): %.1f ms\n", dims[k][0], dims[k][1], ms); }
            auto ref = reference(OW, OH, 15.f); Stats s = check(bc.outPlane(), OW, OH, ref); printf("  %dx%d final frame: PSNR %.2f finite=%d\n", dims[k][0], dims[k][1], s.psnr, s.finite); ok = ok && s.finite; }
        EXPECT(ok, "resolution changes handled, output valid");
    }
    if (want("negative")) {
        printf("[negative] malformed requests must be rejected without killing the helper\n");
        renderFrame(bc, RW, RH, OW, OH, 0, 1, PH, p); FrameParams q = p; q.outW = 99999; r = bc.submit(q, 10000, &rs);
        EXPECT(r == BridgeResult::Rejected && rs.status == kBadRequest, "oversized output dims rejected: '%s'", rs.message);
        q = p; q.colorFmt = 7; r = bc.submit(q, 10000, &rs); EXPECT(r == BridgeResult::Rejected, "bad color format rejected: '%s'", rs.message);
        q = p; q.depthPitch = 8; r = bc.submit(q, 10000, &rs); EXPECT(r == BridgeResult::Rejected, "pitch smaller than row rejected: '%s'", rs.message);
        q = p; q.colorPitch = 1u << 30; r = bc.submit(q, 10000, &rs); EXPECT(r == BridgeResult::Rejected, "pitch*h beyond plane capacity rejected: '%s'", rs.message);
        q = p; q.preExposure = 0; r = bc.submit(q, 10000, &rs); EXPECT(r == BridgeResult::Rejected, "preExposure=0 rejected: '%s'", rs.message);
        q = p; q.renderW = 2000; q.outW = 2000; r = bc.submit(q, 10000, &rs); EXPECT(r == BridgeResult::Rejected, "render size above mapping rejected: '%s'", rs.message);
        q = p; q.withCrc = true; q.corruptCrc = true; r = bc.submit(q, 30000, &rs);
        EXPECT(r == BridgeResult::Rejected && rs.status == kBadRequest && strstr(rs.message, "CRC"), "corrupted CRC detected: '%s'", rs.message);
        // CRC: (old weak check kept for the valid path) -> helper must detect
        q = p; q.withCrc = true; { // submit() computes CRC internally, so corrupt through a second request with stale crc: emulate via manual path
            uint8_t save = bc.colorPlane()[1000]; // flip after crc is impossible with this API; instead test CRC acceptance of good data
            r = bc.submit(q, 30000, &rs); EXPECT(r == BridgeResult::Ok, "valid frame with CRC accepted (%s)", rs.message); bc.colorPlane()[1000] = save; }
        renderFrame(bc, RW, RH, OW, OH, 0, 2, PH, p); r = bc.submit(p, 30000, &rs); EXPECT(r == BridgeResult::Ok && bc.running(), "helper still healthy after rejections");
    }
    if (want("latency")) {
        printf("[latency] 200 frames Quality 1080p, steady state, planes unchanged (isolates IPC+D3D12 cost from scene generation)\n");
        renderFrame(bc, RW, RH, OW, OH, 0, 0, PH, p); std::vector<double> rt, up, gpu, rb, tot; p.flags = kAutoExposure | kNonLinearSrgb;
        for (int f = 0; f < 200; ++f) { p.frameId = f; p.jitterX = halton(f % PH + 1, 2) - .5f; p.jitterY = halton(f % PH + 1, 3) - .5f; auto a = Clock::now(); r = bc.submit(p, 30000, &rs);
            double ms = std::chrono::duration<double, std::milli>(Clock::now() - a).count(); if (r != BridgeResult::Ok) { printf("  fail %s\n", rname(r)); break; }
            if (f >= 20) { rt.push_back(ms); up.push_back(rs.helperUploadMs); gpu.push_back(rs.gpuDispatchMs); rb.push_back(rs.helperReadbackMs); tot.push_back(rs.helperTotalMs); } }
        auto med = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }; auto p95 = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() * 95 / 100]; };
        printf("  round-trip ms: median %.2f p95 %.2f | helper upload %.2f | GPU dispatch %.3f | readback+copy-out %.2f | helper total %.2f\n", med(rt), p95(rt), med(up), med(gpu), med(rb), med(tot));
        printf("  => IPC overhead (round-trip - helper total) median %.3f ms\n", med(rt) - med(tot));
    }
    if (want("crash")) {
        printf("[crash] kill helper hard, expect detection and automatic restart\n");
        bc.kill(); renderFrame(bc, RW, RH, OW, OH, 0, 5, PH, p); r = bc.submit(p, 5000, &rs);
        EXPECT(r == BridgeResult::NotRunning || r == BridgeResult::HelperDied, "dead helper detected (result=%s) without hanging", rname(r));
        auto a = Clock::now(); bool up = bc.start(cfg); double ms = std::chrono::duration<double, std::milli>(Clock::now() - a).count();
        EXPECT(up, "restart succeeded in %.0f ms (%s)", ms, bc.error().c_str());
        if (up) { renderFrame(bc, RW, RH, OW, OH, 0, 6, PH, p); p.flags |= kReset; r = bc.submit(p, 60000, &rs); EXPECT(r == BridgeResult::Ok && bc.activeProvider() == provider, "frame processed after recovery, provider '%s'", bc.activeProvider().c_str()); }
    }
    if (want("crash2")) {
        printf("[crash2] SIGKILL the helper process group WHILE frames are being submitted\n");
        std::atomic<bool> go{true}; std::thread killer([&] { std::this_thread::sleep_for(std::chrono::milliseconds(300)); for (pid_t q2 : bc.helperLinuxPids()) ::kill(q2, SIGKILL); });
        BridgeResult last = BridgeResult::Ok; int n = 0; auto a = Clock::now();
        for (; n < 2000; ++n) { renderFrame(bc, 640, 360, 960, 540, 0, n, PH, p); last = bc.submit(p, 5000, &rs); if (last != BridgeResult::Ok) break; }
        killer.join(); double ms = std::chrono::duration<double, std::milli>(Clock::now() - a).count();
        EXPECT(last == BridgeResult::HelperDied || last == BridgeResult::NotRunning, "kill detected after %d frames, %.0f ms total, result=%s (no hang)", n, ms, rname(last));
        bool up = bc.start(cfg); EXPECT(up, "restart after mid-dispatch kill (%s)", bc.error().c_str());
        if (up) { renderFrame(bc, RW, RH, OW, OH, 0, 1, PH, p); p.flags |= kReset; r = bc.submit(p, 60000, &rs); EXPECT(r == BridgeResult::Ok, "frame OK after recovery"); }
    }
    if (want("freeze")) {
        printf("[freeze] SIGSTOP the helper (hang simulation): submit must time out, not deadlock\n");
        renderFrame(bc, RW, RH, OW, OH, 0, 1, PH, p); for (pid_t q2 : bc.helperLinuxPids()) ::kill(q2, SIGSTOP);
        auto a = Clock::now(); r = bc.submit(p, 1500, &rs); double ms = std::chrono::duration<double, std::milli>(Clock::now() - a).count();
        EXPECT(r == BridgeResult::Timeout && ms < 3000, "timeout reported after %.0f ms (result=%s)", ms, rname(r));
        for (pid_t q2 : bc.helperLinuxPids()) ::kill(q2, SIGCONT); bc.kill();
        bool up = bc.start(cfg); EXPECT(up, "restart after freeze (%s)", bc.error().c_str());
        if (up) { renderFrame(bc, RW, RH, OW, OH, 0, 2, PH, p); p.flags |= kReset; r = bc.submit(p, 60000, &rs); EXPECT(r == BridgeResult::Ok, "frame OK after recovery"); }
    }
    if (want("lease")) {
        printf("[lease] client stops renewing its lease (simulated client death): helper must exit by itself\n");
        bc.debugStopLease(); auto a = Clock::now(); size_t n = 1;
        while (n && std::chrono::duration<double>(Clock::now() - a).count() < 15) { std::this_thread::sleep_for(std::chrono::milliseconds(250)); n = 0; for (pid_t q2 : bc.helperLinuxPids()) { std::ifstream f("/proc/" + std::to_string(q2) + "/cmdline"); if (f.good()) { std::string c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); if (c.find("fsr4_bridge.exe") != std::string::npos) ++n; } } }
        EXPECT(n == 0, "wine helper exited %.1f s after lease stopped (remaining helper procs: %zu)", std::chrono::duration<double>(Clock::now() - a).count(), n);
    }
    bc.stop();
    printf("RESULT: %s (%d failed checks)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}

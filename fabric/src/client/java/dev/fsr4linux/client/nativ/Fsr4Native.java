package dev.fsr4linux.client.nativ;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.nio.file.Path;
import java.util.Arrays;

/**
 * Thin FFM (Panama) binding to libfsr4linux.so (see native/include/fsr4_capi.h). No JNI.
 * Every call returns the native result code; the textual cause is available through {@link #lastError()}.
 */
public final class Fsr4Native implements AutoCloseable {
	private static final Linker LINKER = Linker.nativeLinker();
	private static final ValueLayout.OfLong J = ValueLayout.JAVA_LONG;
	private static final ValueLayout.OfInt I = ValueLayout.JAVA_INT;
	private static final ValueLayout A = ValueLayout.ADDRESS;
	private static final long[] EXPECTED_ABI = {1, 3, 640, 64, 256, 64, 256, 1152};

	private final Arena arena = Arena.ofShared();
	private final MethodHandle create, destroy, lastError, bridgeStart, bridgeRunning, bridgeStop, activeProvider, offeredProviders;
	private final MethodHandle gpuInit, gpuAlloc, gpuRelease, recordInput, recordWorld, setEntities, setParticles, x3Alloc, x3SetInt64, x3Release, x3Hudless, x3Generate, x3GenerateN, x3End, x3Invalidate, x3GpuMs, x3Bytes, recordHandColor, recordOutput, gpuTimes, fgGpuTimes, timeline, waitTimeline, process, processAsync, syncInit, sharedSemaphore, syncValues, signalShared, fgAlloc, fgRelease, fgReady, recordFgInput, recordFgHudless, recordFgOutput, processFg, fgProvider, fgPost, fgCollect, fgOutstanding, sharedCounter, debugDump, disableSharedFence;
	private MemorySegment handle = MemorySegment.NULL;

	private static final String[] REQUIRED_SYMBOLS = {
		"fsr4_abi_info",
		"fsr4_create", "fsr4_destroy", "fsr4_last_error",
		"fsr4_bridge_start", "fsr4_bridge_running", "fsr4_bridge_stop",
		"fsr4_active_provider", "fsr4_offered_providers",
		"fsr4_gpu_init", "fsr4_gpu_alloc", "fsr4_gpu_release",
		"fsr4_record_input", "fsr4_record_world", "fsr4_set_entities", "fsr4_set_particles",
		"fsr4_x3_alloc", "fsr4_x3_set_int64", "fsr4_x3_release", "fsr4_x3_record_hudless",
		"fsr4_x3_record_generate", "fsr4_x3_record_generate_n", "fsr4_x3_record_end", "fsr4_x3_invalidate",
		"fsr4_x3_gpu_ms", "fsr4_x3_bytes", "fsr4_record_hand_color", "fsr4_record_output",
		"fsr4_gpu_times", "fsr4_fg_gpu_times", "fsr4_timeline_semaphore", "fsr4_wait_timeline",
		"fsr4_process", "fsr4_process_async", "fsr4_sync_init", "fsr4_shared_semaphore",
		"fsr4_sync_values", "fsr4_signal_shared",
		"fsr4_fg_alloc", "fsr4_fg_release", "fsr4_fg_ready", "fsr4_fg_provider",
		"fsr4_debug_dump", "fsr4_shared_counter",
		"fsr4_fg_post", "fsr4_fg_collect", "fsr4_fg_outstanding",
		"fsr4_record_fg_input", "fsr4_record_fg_hudless", "fsr4_record_fg_output", "fsr4_process_fg",
		"fsr4_disable_shared_fence"
	};

	public Fsr4Native(Path library) {
		SymbolLookup lib = SymbolLookup.libraryLookup(library, arena);
		
		// Verify all required symbols are present
		for (String sym : REQUIRED_SYMBOLS) {
			if (lib.find(sym).isEmpty()) {
				throw new UnsatisfiedLinkError("Missing required native symbol: " + sym + " in " + library);
			}
		}
		MethodHandle abi = h(lib, "fsr4_abi_info", FunctionDescriptor.of(I, A, I));
		MemorySegment values = arena.allocate(8 * Long.BYTES, Long.BYTES);
		int count = call(abi, values, 8);
		long[] actualAbi = new long[8];
		for (int n = 0; n < actualAbi.length; n++) actualAbi[n] = values.getAtIndex(J, n);
		if (count != 8 || !Arrays.equals(actualAbi, EXPECTED_ABI))
			throw new UnsatisfiedLinkError("Incompatible FSR native/IPC ABI: " + Arrays.toString(actualAbi));
		
		create = h(lib, "fsr4_create", FunctionDescriptor.of(A, A, A, A, A, A, I, I, I, I));
		destroy = h(lib, "fsr4_destroy", FunctionDescriptor.ofVoid(A));
		lastError = h(lib, "fsr4_last_error", FunctionDescriptor.of(A, A));
		bridgeStart = h(lib, "fsr4_bridge_start", FunctionDescriptor.of(I, A, A));
		bridgeRunning = h(lib, "fsr4_bridge_running", FunctionDescriptor.of(I, A));
		bridgeStop = h(lib, "fsr4_bridge_stop", FunctionDescriptor.ofVoid(A));
		activeProvider = h(lib, "fsr4_active_provider", FunctionDescriptor.of(I, A, A, I));
		offeredProviders = h(lib, "fsr4_offered_providers", FunctionDescriptor.of(I, A, A, I));
		gpuInit = h(lib, "fsr4_gpu_init", FunctionDescriptor.of(I, A, J, J, J, J, I));
		gpuAlloc = h(lib, "fsr4_gpu_alloc", FunctionDescriptor.of(I, A, I, I, I, I, J));
		gpuRelease = h(lib, "fsr4_gpu_release", FunctionDescriptor.ofVoid(A));
		recordInput = h(lib, "fsr4_record_input", FunctionDescriptor.of(I, A, J, J, A, A));
		recordWorld = h(lib, "fsr4_record_world", FunctionDescriptor.of(I, A, J, A, A));
		setEntities = h(lib, "fsr4_set_entities", FunctionDescriptor.of(I, A, A, I));
		setParticles = h(lib, "fsr4_set_particles", FunctionDescriptor.of(I, A, A, I));
		x3Alloc = h(lib, "fsr4_x3_alloc", FunctionDescriptor.of(I, A, I, I));
		x3SetInt64 = h(lib, "fsr4_x3_set_int64", FunctionDescriptor.ofVoid(I));
		x3Release = h(lib, "fsr4_x3_release", FunctionDescriptor.ofVoid(A));
		x3Hudless = h(lib, "fsr4_x3_record_hudless", FunctionDescriptor.of(I, A, J, J));
		x3Generate = h(lib, "fsr4_x3_record_generate", FunctionDescriptor.of(I, A, J, J, I, J));
		x3GenerateN = h(lib, "fsr4_x3_record_generate_n", FunctionDescriptor.of(I, A, J, J, I, I, J));
		x3End = h(lib, "fsr4_x3_record_end", FunctionDescriptor.of(I, A, J));
		x3Invalidate = h(lib, "fsr4_x3_invalidate", FunctionDescriptor.ofVoid(A));
		x3GpuMs = h(lib, "fsr4_x3_gpu_ms", FunctionDescriptor.of(I, A, A));
		x3Bytes = h(lib, "fsr4_x3_bytes", FunctionDescriptor.of(J, A));
		recordHandColor = h(lib, "fsr4_record_hand_color", FunctionDescriptor.of(I, A, J, J));
		recordOutput = h(lib, "fsr4_record_output", FunctionDescriptor.of(I, A, J, J));
		gpuTimes = h(lib, "fsr4_gpu_times", FunctionDescriptor.of(I, A, A));
		fgGpuTimes = h(lib, "fsr4_fg_gpu_times", FunctionDescriptor.of(I, A, A));
		timeline = h(lib, "fsr4_timeline_semaphore", FunctionDescriptor.of(J, A));
		waitTimeline = h(lib, "fsr4_wait_timeline", FunctionDescriptor.of(I, A, J, J));
		process = h(lib, "fsr4_process", FunctionDescriptor.of(I, A, A, A, A, A));
		processAsync = h(lib, "fsr4_process_async", FunctionDescriptor.of(I, A, A, A, A, A, J, J));
		syncInit = h(lib, "fsr4_sync_init", FunctionDescriptor.of(I, A));
		sharedSemaphore = h(lib, "fsr4_shared_semaphore", FunctionDescriptor.of(J, A));
		syncValues = h(lib, "fsr4_sync_values", FunctionDescriptor.of(I, A, A, A));
		signalShared = h(lib, "fsr4_signal_shared", FunctionDescriptor.of(I, A, J));
		fgAlloc = h(lib, "fsr4_fg_alloc", FunctionDescriptor.of(I, A, I, I, A, I));
		fgRelease = h(lib, "fsr4_fg_release", FunctionDescriptor.ofVoid(A));
		fgReady = h(lib, "fsr4_fg_ready", FunctionDescriptor.of(I, A));
		fgProvider = h(lib, "fsr4_fg_provider", FunctionDescriptor.of(I, A, A, I));
		debugDump = h(lib, "fsr4_debug_dump", FunctionDescriptor.of(I, A, I, A, A, I));
		sharedCounter = h(lib, "fsr4_shared_counter", FunctionDescriptor.of(J, A));
		fgPost = h(lib, "fsr4_fg_post", FunctionDescriptor.of(I, A, A, A, J, J));
		fgCollect = h(lib, "fsr4_fg_collect", FunctionDescriptor.of(I, A, A));
		fgOutstanding = h(lib, "fsr4_fg_outstanding", FunctionDescriptor.of(I, A));
		recordFgInput = h(lib, "fsr4_record_fg_input", FunctionDescriptor.of(I, A, J, J));
		recordFgHudless = h(lib, "fsr4_record_fg_hudless", FunctionDescriptor.of(I, A, J, J));
		recordFgOutput = h(lib, "fsr4_record_fg_output", FunctionDescriptor.of(I, A, J, J));
		processFg = h(lib, "fsr4_process_fg", FunctionDescriptor.of(I, A, A, A, A, J, J));
		disableSharedFence = h(lib, "fsr4_disable_shared_fence", FunctionDescriptor.ofVoid(A));
	}

	private static MethodHandle h(SymbolLookup lib, String name, FunctionDescriptor fd) {
		return LINKER.downcallHandle(lib.find(name).orElseThrow(() -> new UnsatisfiedLinkError("missing symbol " + name)), fd);
	}

	private static <T> T call(MethodHandle m, Object... args) {
		try {
			@SuppressWarnings("unchecked") T r = (T) m.invokeWithArguments(args);
			return r;
		} catch (RuntimeException | Error e) {
			throw e;
		} catch (Throwable t) {
			throw new IllegalStateException(t);
		}
	}

	public void create(String proton, String prefix, String steam, String helperExe, String log, int maxRW, int maxRH, int maxOW, int maxOH) {
		handle = call(create, arena.allocateFrom(proton), arena.allocateFrom(prefix), arena.allocateFrom(steam), arena.allocateFrom(helperExe), arena.allocateFrom(log), maxRW, maxRH, maxOW, maxOH);
	}

	public String lastError() {
		MemorySegment s = call(lastError, handle);
		return s.reinterpret(1024).getString(0);
	}

	public int bridgeStart(String provider) { return call(bridgeStart, handle, arena.allocateFrom(provider)); }
	public boolean bridgeRunning() { return (int) call(bridgeRunning, handle) != 0; }
	public void bridgeStop() { call(bridgeStop, handle); }

	private String str(MethodHandle m) {
		try (Arena a = Arena.ofConfined()) {
			MemorySegment buf = a.allocate(256);
			call(m, handle, buf, 256);
			return buf.getString(0);
		}
	}
	public String activeProvider() { return str(activeProvider); }
	public String offeredProviders() { return str(offeredProviders); }

	public int gpuInit(long instance, long phys, long device, long queue, int family) { return call(gpuInit, handle, instance, phys, device, queue, family); }
	public int gpuAlloc(int rw, int rh, int ow, int oh, long depthImage) { return call(gpuAlloc, handle, rw, rh, ow, oh, depthImage); }
	public void gpuRelease() { call(gpuRelease, handle); }
	public long timelineSemaphore() { return call(timeline, handle); }
	public int waitTimeline(long value, long timeoutNs) { return call(waitTimeline, handle, value, timeoutNs); }
	/** GPU timestamp segments of the last completed frame (needs FSR4_GPU_TS=1): prep, upscale chain, copy-out, other. Null when unavailable. */
	public float[] gpuTimes() {
		try (Arena a = Arena.ofConfined()) { MemorySegment buf = a.allocate(16); int rc = call(gpuTimes, handle, buf); if (rc != 0) return null; return new float[]{buf.getAtIndex(ValueLayout.JAVA_FLOAT, 0), buf.getAtIndex(ValueLayout.JAVA_FLOAT, 1), buf.getAtIndex(ValueLayout.JAVA_FLOAT, 2), buf.getAtIndex(ValueLayout.JAVA_FLOAT, 3)}; }
	}
	public float[] fgGpuTimes() {
		try (Arena a = Arena.ofConfined()) { MemorySegment buf = a.allocate(16); int rc = call(fgGpuTimes, handle, buf); if (rc != 0) return null; return new float[]{buf.getAtIndex(ValueLayout.JAVA_FLOAT, 0), buf.getAtIndex(ValueLayout.JAVA_FLOAT, 1), buf.getAtIndex(ValueLayout.JAVA_FLOAT, 2), buf.getAtIndex(ValueLayout.JAVA_FLOAT, 3)}; }
	}
	public int recordOutput(long cb, long dstImage) { return call(recordOutput, handle, cb, dstImage); }

	/** World pass (depth + motion vectors): must be recorded BEFORE Minecraft draws the first-person hand (which clears the depth buffer). */
	public int recordWorld(long cb, float[] matrices48, float[] camDelta3) {
		try (Arena a = Arena.ofConfined()) { return call(recordWorld, handle, cb, a.allocateFrom(ValueLayout.JAVA_FLOAT, matrices48), a.allocateFrom(ValueLayout.JAVA_FLOAT, camDelta3)); }
	}
	/** Per-entity rigid motion for the next world pass (16 floats per entity, max 48). */
	public int setParticles(float[] data, int n) {
		try (Arena a = Arena.ofConfined()) { return call(setParticles, handle, n == 0 ? MemorySegment.NULL : a.allocateFrom(ValueLayout.JAVA_FLOAT, data), n); }
	}
	public int setEntities(float[] data, int n) {
		try (Arena a = Arena.ofConfined()) { return call(setEntities, handle, a.allocateFrom(ValueLayout.JAVA_FLOAT, data), n); }
	}
	// ---- FG x3 (experimental own backend) ----
	public void x3SetInt64(boolean on) { try { x3SetInt64.invoke(on ? 1 : 0); } catch (Throwable t) { throw new RuntimeException(t); } }
	public int x3Alloc(int dispW, int dispH) { return call(x3Alloc, handle, dispW, dispH); }
	public void x3Release() { call(x3Release, handle); }
	public int x3RecordHudless(long cb, long colorImage) { return call(x3Hudless, handle, cb, colorImage); }
	/** 1 = a frame was generated into dst, 0 = no previous frame yet. */
	public int x3RecordGenerate(long cb, long finalImage, int idx, long dstImage) { return call(x3Generate, handle, cb, finalImage, idx, dstImage); }
	public int x3RecordGenerateN(long cb, long finalImage, int idx, int n, long dstImage) { return call(x3GenerateN, handle, cb, finalImage, idx, n, dstImage); }
	public int x3RecordEnd(long cb) { return call(x3End, handle, cb); }
	public void x3Invalidate() { call(x3Invalidate, handle); }
	public float x3GpuMs() { try (Arena a = Arena.ofConfined()) { MemorySegment buf = a.allocate(4); int rc = call(x3GpuMs, handle, buf); return rc == 0 ? buf.get(ValueLayout.JAVA_FLOAT, 0) : -1f; } }
	public long x3Bytes() { return call(x3Bytes, handle); }

	/** Disable GPU-to-GPU synchronization (shared fence) and force CPU-wait path. */
	public void disableSharedFence() { call(disableSharedFence, handle); }

	/** Hand overlay (zero motion + hand depth) and copy of the finished world colour into the shared colour plane. */
	public int recordHandColor(long cb, long colorImage) { return call(recordHandColor, handle, cb, colorImage); }

	/** matrices48 = invVpJit, prevVp, curVp (column-major); camDelta3 = camera position delta (cur - prev). */
	public int recordInput(long cb, long colorImage, float[] matrices48, float[] camDelta3) {
		try (Arena a = Arena.ofConfined()) {
			MemorySegment m = a.allocateFrom(ValueLayout.JAVA_FLOAT, matrices48);
			MemorySegment d = a.allocateFrom(ValueLayout.JAVA_FLOAT, camDelta3);
			return call(recordInput, handle, cb, colorImage, m, d);
		}
	}

	// ---- frame generation (FSR FG API 4.0.1) ----
	public int fgAlloc(int displayW, int displayH, String provider, int ctxFlags) { try (Arena a = Arena.ofConfined()) { return call(fgAlloc, handle, displayW, displayH, a.allocateFrom(provider), ctxFlags); } }
	public void fgRelease() { call(fgRelease, handle); }
	/** Asynchronous FG request: returns once published; {@link #fgCollect} MUST be called before any other bridge call. */
	public int fgPost(int[] ints, float[] floats, long wait, long signal) {
		try (Arena a = Arena.ofConfined()) { return call(fgPost, handle, a.allocateFrom(ValueLayout.JAVA_INT, ints), a.allocateFrom(ValueLayout.JAVA_FLOAT, floats), wait, signal); }
	}
	public int fgCollect(float[] stats) {
		try (Arena a = Arena.ofConfined()) {
			MemorySegment s = a.allocate(32); int rc = call(fgCollect, handle, s);
			for (int k = 0; k < stats.length && k < 8; k++) stats[k] = s.getAtIndex(ValueLayout.JAVA_FLOAT, k);
			return rc;
		}
	}
	public boolean fgOutstanding() { return (int) call(fgOutstanding, handle) != 0; }
	public String fgProvider() { return str(fgProvider); }
	public boolean fgReady() { return (int) call(fgReady, handle) != 0; }
	public int recordFgInput(long cb, long finalColorImage) { return call(recordFgInput, handle, cb, finalColorImage); }
	public int recordFgHudless(long cb, long worldColorImage) { return call(recordFgHudless, handle, cb, worldColorImage); }
	public int recordFgOutput(long cb, long dstImage) { return call(recordFgOutput, handle, cb, dstImage); }
	/** ints = {frameId, flags(1 = reset)}; floats = {jitterX, jitterY, mvScaleX, mvScaleY, frameTimeMs, near, far, fovY, camPos[3], camUp[3], camRight[3], camFwd[3]}; stats = {gpuFgMs(previous frame), helperMs, status, ffxRc, cpuRecordMs}. */
	public int processFg(int[] ints, float[] floats, float[] stats, long wait, long signal) {
		try (Arena a = Arena.ofConfined()) {
			MemorySegment i = a.allocateFrom(ValueLayout.JAVA_INT, ints), f = a.allocateFrom(ValueLayout.JAVA_FLOAT, floats), s = a.allocate(32);
			int rc = call(processFg, handle, i, f, s, wait, signal);
			for (int k = 0; k < stats.length && k < 8; k++) stats[k] = s.getAtIndex(ValueLayout.JAVA_FLOAT, k);
			return rc;
		}
	}

	public int syncInit() { return call(syncInit, handle); }
	public long sharedSemaphore() { return call(sharedSemaphore, handle); }
	/** Debug: statistics of a shared plane (1 depth, 2 motion vectors) after a full GPU idle; optional raw dump to a file. */
	public String debugDump(int plane, String path) {
		try (Arena a = Arena.ofConfined()) { MemorySegment buf = a.allocate(512); int rc = call(debugDump, handle, plane, a.allocateFrom(path == null ? "" : path), buf, 512); return (rc == 0 ? "" : "FAILED: ") + buf.getString(0); }
	}
	public long sharedCounter() { return call(sharedCounter, handle); }
	public int signalShared(long value) { return call(signalShared, handle, value); }
	/** Reserves the next (wait, signal) pair of the shared timeline: the world GPU work signals {@code out[0]}, the helper signals {@code out[1]}. */
	public int syncValues(long[] out) {
		try (Arena a = Arena.ofConfined()) {
			MemorySegment w = a.allocate(8), s = a.allocate(8);
			int rc = call(syncValues, handle, w, s);
			out[0] = w.get(J, 0); out[1] = s.get(J, 0);
			return rc;
		}
	}

	/** Asynchronous variant of {@link #process}: returns when the helper has SUBMITTED the work (no CPU wait for the GPU). */
	public int processAsync(int[] ints, float[] floats, String provider, float[] stats, long wait, long signal) {
		try (Arena a = Arena.ofConfined()) {
			MemorySegment i = a.allocateFrom(ValueLayout.JAVA_INT, ints);
			MemorySegment f = a.allocateFrom(ValueLayout.JAVA_FLOAT, floats);
			MemorySegment s = a.allocate(32);
			int rc = call(processAsync, handle, i, f, a.allocateFrom(provider), s, wait, signal);
			for (int k = 0; k < stats.length && k < 8; k++) stats[k] = s.getAtIndex(ValueLayout.JAVA_FLOAT, k);
			return rc;
		}
	}

	/** Returns native rc; stats = {gpuDispatchMs, helperTotalMs, status, ffxReturnCode}. */
	public int process(int[] ints, float[] floats, String provider, float[] stats) {
		try (Arena a = Arena.ofConfined()) {
			MemorySegment i = a.allocateFrom(ValueLayout.JAVA_INT, ints);
			MemorySegment f = a.allocateFrom(ValueLayout.JAVA_FLOAT, floats);
			MemorySegment s = a.allocate(32);
			int rc = call(process, handle, i, f, a.allocateFrom(provider), s);
			for (int k = 0; k < stats.length && k < 8; k++) stats[k] = s.getAtIndex(ValueLayout.JAVA_FLOAT, k);
			return rc;
		}
	}

	@Override
	public void close() {
		if (!MemorySegment.NULL.equals(handle)) {
			call(destroy, handle);
			handle = MemorySegment.NULL;
		}
	}
}

package dev.fsr4linux.core;

import static org.junit.jupiter.api.Assertions.*;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.EnumSet;
import java.util.List;
import java.util.Set;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

class UpscaleControllerTest {
	/** Records every call so tests can check ordering (GPU sync before any resource change) and leaks. */
	public static final class FakeBackend implements UpscalerBackend {
		final BackendId id; boolean available = true; String cause = "fake unavailable";
		Set<QualityMode> qualities = EnumSet.of(QualityMode.QUALITY, QualityMode.BALANCED, QualityMode.PERFORMANCE);
		boolean failNextInit; boolean failNextResize; boolean allocated; int resets; String executedId;
		final List<String> log;
		FakeBackend(BackendId id, List<String> log) { this.id = id; this.log = log; }
		public BackendId id() { return id; }
		public BackendStatus probe() { return available ? status() : BackendStatus.unavailable(id, "fake", cause); }
		public BackendStatus status() {
			return new BackendStatus(id, allocated ? BackendState.READY : available ? BackendState.AVAILABLE : BackendState.UNAVAILABLE, "1", "fake", "dev", null, allocated ? executedId : null);
		}
		public Set<QualityMode> supportedQualities() { return qualities; }
		public void initialize(int w, int h, QualityMode q) throws BackendException {
			log.add("init " + id + " " + q);
			if (failNextInit) { failNextInit = false; throw new BackendException("init failed"); }
			allocated = true;
		}
		public void resize(int w, int h, QualityMode q) throws BackendException {
			log.add("resize " + id + " " + q);
			if (failNextResize) { failNextResize = false; allocated = false; throw new BackendException("resize failed"); }
		}
		public void resetHistory() { resets++; }
		public void shutdown() { if (allocated) log.add("shutdown " + id); allocated = false; }
	}

	List<String> log; FakeBackend f3, f4; Fsr4Config cfg; List<Notification> notes; UpscaleController c; int syncs; int saves;

	@BeforeEach void setup() {
		log = new ArrayList<>(); notes = new ArrayList<>(); syncs = 0; saves = 0;
		f3 = new FakeBackend(BackendId.FSR3_1_5, log); f3.executedId = "ffx-fsr3-3.1.5";
		f4 = new FakeBackend(BackendId.FSR4_1_1, log); f4.available = false; f4.cause = "no bridge";
		cfg = new Fsr4Config();
		c = new UpscaleController(cfg, List.of(f3, f4), notes::add, x -> saves++);
		c.setRendererReady(true);
		c.requestResize(1920, 1080);
		c.onFrameBoundary(() -> syncs++);
		notes.clear(); log.clear(); syncs = 0;
	}

	void frame() { c.onFrameBoundary(() -> { syncs++; log.add("gpu-idle"); }); }
	String last() { return notes.get(notes.size() - 1).text(); }

	@Test void toggleOnAndOff() {
		c.requestToggle(); frame();
		assertEquals("FSR ACTIVADO | FSR 3.1.5 | QUALITY", last());
		assertTrue(c.isActive());
		c.requestToggle(); frame();
		assertEquals("FSR DESACTIVADO", last());
		assertFalse(c.isActive());
		assertFalse(f3.allocated, "resources freed on disable");
	}

	@Test void qualityCyclesAndWrapsSkippingUnsupported() {
		c.requestToggle(); frame();
		List<String> seen = new ArrayList<>();
		for (int i = 0; i < 4; i++) { c.requestCycleQuality(); frame(); seen.add(last()); }
		assertEquals(List.of("FSR CALIDAD: BALANCED", "FSR CALIDAD: PERFORMANCE", "FSR CALIDAD: QUALITY", "FSR CALIDAD: BALANCED"), seen);
	}

	@Test void unavailableFsr4KeepsCurrentBackendAndExplains() {
		c.requestToggle(); frame();
		c.requestCycleBackend(); frame();
		assertTrue(last().startsWith("FSR 4.1.1 NO DISPONIBLE"), last());
		assertEquals(BackendId.FSR3_1_5, cfg.backendId());
		assertTrue(c.isActive());
	}

	@Test void switchesToFsr4WhenAvailableAndBack() {
		f4.available = true; f4.executedId = "ffx-fsr4-4.1.1";
		cfg.backend = BackendId.FSR3_1_5.name();
		c.requestToggle(); frame();
		c.requestCycleBackend(); frame();
		assertEquals("FSR VERSIÓN: FSR 4.1.1", last());
		assertTrue(f4.allocated); assertFalse(f3.allocated);
		c.requestCycleBackend(); frame();
		assertEquals("FSR VERSIÓN: FSR 3.1.5", last());
		assertTrue(f3.allocated); assertFalse(f4.allocated);
	}

	@Test void failedSwitchRollsBackToPreviousBackend() {
		f4.available = true; f4.failNextInit = true;
		cfg.backend = BackendId.FSR3_1_5.name();
		c.requestToggle(); frame();
		c.requestCycleBackend(); frame();
		assertEquals(BackendId.FSR3_1_5, cfg.backendId());
		assertTrue(f3.allocated && c.isActive(), "old backend must be restored when the new one fails");
		assertTrue(last().contains("ERROR"), last());
	}

	@Test void automaticPriorityAndUnavailableFallback() {
		f4.available = true; f4.executedId = "ffx-fsr4-4.1.1";
		c.requestToggle(); frame();
		assertEquals(BackendId.FSR4_1_1, cfg.backendId());
		assertEquals("FSR 4.1.1 [ffx-fsr4-4.1.1]", c.runningLabel());
		c.requestToggle(); frame();
		f4.available = false;
		c.requestToggle(); frame();
		assertEquals(BackendId.FSR3_1_5, cfg.backendId());
		assertTrue(c.isActive());
	}

	@Test void failed411InitializationFallsBackTo315() {
		f4.available = true; f4.failNextInit = true;
		c.requestToggle(); frame();
		assertEquals(BackendId.FSR3_1_5, cfg.backendId());
		assertTrue(c.isActive());
		assertFalse(f4.allocated);
		assertTrue(notes.stream().anyMatch(n -> n.text().contains("usando FSR 3.1.5")));
	}

	@Test void providerRuntimeFailureFallsBackAndPersists() {
		f4.available = true; f4.executedId = "ffx-fsr4-4.1.1";
		c.requestToggle(); frame();
		int savedBefore = saves;
		c.reportRuntimeFailure(() -> { syncs++; log.add("gpu-idle"); }, "ffxCreateContext rc=5");
		assertFalse(c.isActive());
		frame();
		assertTrue(c.isActive());
		assertEquals(BackendId.FSR3_1_5, cfg.backendId());
		assertTrue(saves > savedBefore);
		assertEquals("FSR 3.1.5 [ffx-fsr3-3.1.5]", c.runningLabel());
	}

	@Test void failedResizeRestoresPreviousConfiguration() {
		c.requestToggle(); frame();
		f3.failNextResize = true;
		c.requestCycleQuality(); frame();
		assertEquals(QualityMode.QUALITY, cfg.qualityMode(), "quality must be restored");
		assertTrue(c.isActive(), "backend must be rebuilt after the failure");
	}

	@Test void gpuIsIdleBeforeEveryResourceChange() {
		c.requestToggle(); frame();
		c.requestCycleQuality(); frame();
		c.requestToggle(); frame();
		for (int i = 0; i < log.size(); i++) {
			String e = log.get(i);
			if (e.startsWith("init") || e.startsWith("resize") || e.startsWith("shutdown"))
				assertTrue(log.subList(0, i).contains("gpu-idle"), "GPU must be idle before: " + e + " in " + log);
		}
		assertFalse(f3.allocated, "no leaked resources after disable");
	}

	@Test void changesRejectedWhileRendererNotReady() {
		c.setRendererReady(false);
		c.requestToggle(); frame();
		assertFalse(cfg.enabled);
		assertTrue(last().contains("OCUPADO"));
		assertEquals(0, syncs);
	}

	@Test void resizeRecreatesAndResetsHistory() {
		c.requestToggle(); frame();
		int before = f3.resets;
		c.requestResize(2560, 1440); frame();
		assertTrue(log.contains("resize FSR3_1_5 QUALITY"));
		assertTrue(f3.resets > before);
	}

	@Test void runningLabelOnlyWithExecutedImplementation() {
		c.requestToggle(); frame();
		assertEquals("FSR 3.1.5 [ffx-fsr3-3.1.5]", c.runningLabel());
		f3.executedId = null;
		assertNull(c.runningLabel(), "no label without proof of execution");
	}

	@Test void noBackendUsableDoesNotEnable() {
		f3.available = false; f3.cause = "no vulkan";
		c.requestToggle(); frame();
		assertFalse(cfg.enabled);
		assertTrue(last().startsWith("FSR NO DISPONIBLE"));
	}

	@Test void configRoundTripAndCorruptFile(@TempDir Path dir) throws IOException {
		Path p = dir.resolve("c.json");
		Fsr4Config a = new Fsr4Config(); a.enabled = true; a.backend = "FSR3_1_5"; a.quality = "PERFORMANCE"; a.sharpness = 0.8f; a.save(p);
		Fsr4Config b = Fsr4Config.load(p);
		assertEquals(BackendId.FSR3_1_5, b.backendId()); assertEquals(QualityMode.PERFORMANCE, b.qualityMode()); assertTrue(b.enabled);
		Files.writeString(p, "{ not json");
		assertEquals(BackendId.NATIVE, Fsr4Config.load(p).backendId());
		Files.writeString(p, "{\"backend\":\"BOGUS\",\"quality\":\"X\",\"sharpness\":9}");
		Fsr4Config d = Fsr4Config.load(p);
		assertEquals(BackendId.NATIVE, d.backendId()); assertEquals(1f, d.sharpness);
	}

	@Test void qualityRenderSizes() {
		assertEquals(1280, QualityMode.QUALITY.renderSize(1920));
		assertEquals(960, QualityMode.PERFORMANCE.renderSize(1920));
		assertEquals(1920, QualityMode.NATIVE_AA.renderSize(1920));
	}
}

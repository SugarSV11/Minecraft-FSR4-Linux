package dev.fsr4linux.core;

import static org.junit.jupiter.api.Assertions.*;

import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;

class RuntimeFailureTest {
	@Test void runtimeFailureDisablesSafelyAndKeepsSettings() {
		List<String> log = new ArrayList<>();
		var f3 = new UpscaleControllerTest.FakeBackend(BackendId.FSR3_1_5, log);
		List<Notification> notes = new ArrayList<>();
		Fsr4Config cfg = new Fsr4Config(); cfg.quality = "BALANCED";
		UpscaleController c = new UpscaleController(cfg, List.of(f3), notes::add, x -> { });
		c.setRendererReady(true); c.requestResize(1920, 1080); c.requestToggle(); c.onFrameBoundary(() -> { });
		assertTrue(c.isActive());
		c.reportRuntimeFailure(() -> log.add("idle"), "helper died");
		assertFalse(c.isActive()); assertFalse(cfg.enabled); assertFalse(f3.allocated);
		assertEquals(QualityMode.BALANCED, cfg.qualityMode(), "quality preserved");
		assertEquals(BackendId.FSR3_1_5, cfg.backendId(), "backend preserved");
		assertTrue(notes.get(notes.size() - 1).text().contains("helper died"));
		c.requestToggle(); c.onFrameBoundary(() -> { });
		assertTrue(c.isActive(), "can be re-enabled after a failure");
	}
}

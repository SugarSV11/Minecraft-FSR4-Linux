package dev.fsr4linux.core;

import static org.junit.jupiter.api.Assertions.*;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

class Fsr4ConfigTest {
	@TempDir Path dir;

	@Test void defaultsAreSafe() {
		Fsr4Config c = new Fsr4Config();
		assertFalse(c.enabled); assertFalse(c.frameGeneration); assertFalse(c.fgPacing); assertEquals(2, c.fgMultiplier);   // x3 is experimental: never the default
		assertEquals(BackendId.NATIVE, c.backendId()); assertEquals(QualityMode.QUALITY, c.qualityMode());
	}

	@Test void roundTripKeepsEverythingIncludingFrameGeneration() throws IOException {
		Path f = dir.resolve("c.json");
		Fsr4Config c = new Fsr4Config();
		c.enabled = true; c.backend = BackendId.FSR4_1_1.name(); c.quality = QualityMode.BALANCED.name(); c.sharpness = 0.7f; c.debugOverlay = true; c.fpsCounter = true; c.frameGeneration = true; c.fgPacing = true;
		c.save(f);
		Fsr4Config r = Fsr4Config.load(f);
		assertTrue(r.enabled); assertEquals(BackendId.FSR4_1_1, r.backendId()); assertEquals(QualityMode.BALANCED, r.qualityMode());
		assertEquals(0.7f, r.sharpness, 1e-6); assertTrue(r.debugOverlay); assertTrue(r.fpsCounter); assertTrue(r.frameGeneration); assertTrue(r.fgPacing);
	}

	@Test void corruptOrInvalidFilesFallBackToDefaults() throws IOException {
		Path f = dir.resolve("bad.json");
		Files.writeString(f, "{ this is not json");
		assertFalse(Fsr4Config.load(f).enabled);
		Files.writeString(f, "{\"backend\":\"FSR9_9_9\",\"quality\":\"ULTRA_MEGA\",\"sharpness\":5.0}");
		Fsr4Config c = Fsr4Config.load(f);
		assertEquals(BackendId.NATIVE, c.backendId()); assertEquals(QualityMode.QUALITY, c.qualityMode()); assertEquals(1.0f, c.sharpness, 1e-6);   // clamped
		assertFalse(Fsr4Config.load(dir.resolve("missing.json")).enabled);
	}

	@Test void oldConfigWithoutFrameGenerationFieldsLoads() throws IOException {
		Path f = dir.resolve("old.json");
		Files.writeString(f, "{\"enabled\":true,\"backend\":\"FSR3_1_5\",\"quality\":\"PERFORMANCE\",\"sharpness\":0.5}");
		Fsr4Config c = Fsr4Config.load(f);
		assertTrue(c.enabled); assertFalse(c.frameGeneration); assertFalse(c.fgPacing);
	}

	@Test void multiplierPersistsAndInvalidValuesFallBackToTwo() throws IOException {
		Path f = dir.resolve("m.json"); Fsr4Config c = new Fsr4Config(); c.fgMultiplier = 3; c.save(f);
		assertEquals(3, Fsr4Config.load(f).fgMultiplier);
		Files.writeString(f, "{\"fgMultiplier\":7}"); assertEquals(2, Fsr4Config.load(f).fgMultiplier);
		Files.writeString(f, "{\"fgMultiplier\":0}"); assertEquals(2, Fsr4Config.load(f).fgMultiplier);
		Files.writeString(f, "{}"); assertEquals(2, Fsr4Config.load(f).fgMultiplier);   // old configs
	}

	@Test void saveIsAtomicNoTempFileLeftBehind() throws IOException {
		Path f = dir.resolve("a.json"); new Fsr4Config().save(f);
		try (var s = Files.list(dir)) { assertEquals(1, s.count()); }
	}
}

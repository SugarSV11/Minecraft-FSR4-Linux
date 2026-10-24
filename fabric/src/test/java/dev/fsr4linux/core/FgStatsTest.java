package dev.fsr4linux.core;

import static org.junit.jupiter.api.Assertions.*;

import org.junit.jupiter.api.Test;

class FgStatsTest {
	@Test void renderedGeneratedAndPresentedAreCountedSeparately() throws InterruptedException {
		FgStats s = new FgStats(); var w = s.begin();
		for (int i = 0; i < 100; i++) {
			s.realFrames++; s.onPresent(System.nanoTime()); s.generatedPresented++; s.onPresent(System.nanoTime()); s.realPresented++;
		}
		Thread.sleep(50);
		var r = s.end(w);
		assertEquals(r.renderedFps(), r.generatedFps(), 1e-6);
		assertEquals(2 * r.renderedFps(), r.presentedFps(), 1e-6, "presented = rendered + generated, never conflated");
		assertTrue(r.presentedFps() > 0);
	}

	@Test void withoutGenerationPresentedEqualsRendered() throws InterruptedException {
		FgStats s = new FgStats(); var w = s.begin();
		for (int i = 0; i < 50; i++) { s.realFrames++; s.onPresent(System.nanoTime()); s.realPresented++; }
		Thread.sleep(20);
		var r = s.end(w);
		assertEquals(0.0, r.generatedFps(), 0.0);
		assertEquals(r.renderedFps(), r.presentedFps(), 1e-6);
	}

	@Test void presentIntervalStatisticsDetectUnevenPacing() {
		FgStats s = new FgStats(); var w = s.begin(); long t = 1_000_000_000L;
		for (int i = 0; i < 200; i++) { t += (i % 2 == 0) ? 100_000L : 3_900_000L; s.onPresent(t); }   // generated frame right after the real one: 0.1 ms / 3.9 ms alternating
		var r = s.end(w);
		assertEquals(2.0, r.presentIntervalMeanMs(), 0.05);
		assertTrue(r.presentIntervalStdMs() > 1.5, "uneven pacing must show a large deviation");
		assertTrue(r.presentIntervalP99Ms() > 3.5);
	}
}

package dev.fsr4linux.core;

import static org.junit.jupiter.api.Assertions.assertEquals;
import java.util.stream.IntStream;
import org.junit.jupiter.api.Test;

class MetricStatsTest {
	@Test void nearestRankPercentilesPreserveWorstFrames() {
		var values = IntStream.rangeClosed(1, 100).mapToObj(i -> (float) i).toList();
		var s = MetricStats.of(values);
		assertEquals(50, s.median());
		assertEquals(95, s.p95());
		assertEquals(99, s.p99());
		assertEquals(100, s.max());
	}

	@Test void emptyAndSingleSampleAreDefined() {
		assertEquals(0, MetricStats.of(java.util.List.of()).p99());
		assertEquals(2.5, MetricStats.of(java.util.List.of(2.5f)).p95());
	}
}

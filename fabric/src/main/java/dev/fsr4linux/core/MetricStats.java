package dev.fsr4linux.core;

import java.util.Arrays;
import java.util.List;

/** Nearest-rank frame-time percentiles; higher values represent slower frames. */
public record MetricStats(double median, double p95, double p99, double max) {
	public static MetricStats of(List<Float> values) {
		if (values.isEmpty()) return new MetricStats(0, 0, 0, 0);
		double[] sorted = values.stream().mapToDouble(Float::doubleValue).toArray();
		Arrays.sort(sorted);
		return new MetricStats(rank(sorted, 0.50), rank(sorted, 0.95), rank(sorted, 0.99), sorted[sorted.length - 1]);
	}

	private static double rank(double[] sorted, double fraction) {
		return sorted[Math.max(0, (int) Math.ceil(fraction * sorted.length) - 1)];
	}
}

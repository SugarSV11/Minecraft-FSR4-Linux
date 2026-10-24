package dev.fsr4linux.core;

import static org.junit.jupiter.api.Assertions.*;
import org.junit.jupiter.api.Test;

class ProviderFallbackTest {
	@Test void switchesOnlyForProviderFailures() {
		assertTrue(ProviderFallback.from411To315("ffxCreateContext rc=5"));
		assertTrue(ProviderFallback.from411To315("provider mismatch: wanted 4.1.1 got 3.1.5"));
		assertTrue(ProviderFallback.from411To315("ffxDispatch rc=3"));
		assertFalse(ProviderFallback.from411To315("GPU wait timeout"));
		assertFalse(ProviderFallback.from411To315("helper died"));
		assertFalse(ProviderFallback.from411To315(null));
	}
}

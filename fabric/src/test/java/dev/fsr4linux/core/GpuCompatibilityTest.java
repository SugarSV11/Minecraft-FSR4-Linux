package dev.fsr4linux.core;

import static org.junit.jupiter.api.Assertions.*;
import org.junit.jupiter.api.Test;

class GpuCompatibilityTest {
	private static GpuCompatibility gpu(int vendor, int type) {
		return new GpuCompatibility(true, vendor, type, 1, 3, true, true, 0);
	}

	@Test void dedicatedAmdUsesSdkOfferAndKeepsFgSeparate() {
		GpuCompatibility g = gpu(GpuCompatibility.AMD_VENDOR, GpuCompatibility.DISCRETE);
		assertTrue(g.mayTryFsr411(true));
		assertFalse(g.mayTryFsr411(false));
		assertTrue(g.mayTryFsr315(true));
		assertTrue(g.mayTryAmdFg());
		assertTrue(g.mayTryOwnFg());
	}

	@Test void integratedAmdOnlyOffersAnalyticalUpscalerAndOwnFg() {
		GpuCompatibility g = gpu(GpuCompatibility.AMD_VENDOR, GpuCompatibility.INTEGRATED);
		assertFalse(g.mayTryFsr411(true));
		assertTrue(g.mayTryFsr315(true));
		assertFalse(g.mayTryAmdFg());
		assertTrue(g.mayTryOwnFg());
	}

	@Test void foreignOrIncapableDeviceIsRejected() {
		assertFalse(gpu(0x10de, GpuCompatibility.DISCRETE).bridgeCapable());
		assertFalse(new GpuCompatibility(false, 0x1002, 2, 1, 3, true, true, 1200).bridgeCapable());
		assertFalse(new GpuCompatibility(true, 0x1002, 2, 1, 2, true, true, 1200).bridgeCapable());
		assertFalse(new GpuCompatibility(true, 0x1002, 2, 1, 3, false, true, 1200).bridgeCapable());
		assertFalse(new GpuCompatibility(true, 0x1002, 2, 1, 3, true, false, 1200).bridgeCapable());
	}

	@Test void knownGfxIpRespectsRdnaMatrixWithoutCommercialName() {
		assertEquals(1200, GpuCompatibility.gfxIpFromDriverName("AMD Radeon RX 9060 XT (RADV GFX1200)"));
		for (int gfx : new int[]{1010, 1030, 1150}) {
			GpuCompatibility g = new GpuCompatibility(true, 0x1002, 2, 1, 3, true, true, gfx);
			assertFalse(g.mayTryFsr411(true));
			assertTrue(g.mayTryFsr315(true));
		}
		for (int gfx : new int[]{1100, 1200}) {
			assertTrue(new GpuCompatibility(true, 0x1002, 2, 1, 3, true, true, gfx).mayTryFsr411(true));
		}
	}
}

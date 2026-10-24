package dev.fsr4linux.core;

/** Per-dimension upscale ratios as documented for the FSR SDK (Native AA 1.0x ... Ultra Performance 3.0x). */
public enum QualityMode {
	NATIVE_AA("NATIVE AA", 1.0f),
	QUALITY("QUALITY", 1.5f),
	BALANCED("BALANCED", 1.7f),
	PERFORMANCE("PERFORMANCE", 2.0f),
	ULTRA_PERFORMANCE("ULTRA PERFORMANCE", 3.0f);

	public final String label;
	public final float ratio;

	QualityMode(String label, float ratio) {
		this.label = label;
		this.ratio = ratio;
	}

	/** Internal (render) size for a given output size; never below 1 pixel. */
	public int renderSize(int outputSize) {
		return Math.max(1, Math.round(outputSize / ratio));
	}
}

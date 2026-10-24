package dev.fsr4linux.core;

/** Upscaling backends the mod knows about. The label is what the user sees; it must never over-claim. */
public enum BackendId {
	NATIVE("Native", "Native (no upscaling)"),
	FSR3_1_5("FSR 3.1.5", "AMD FSR 3.1.5 upscaler"),
	FSR4_1_1("FSR 4.1.1", "AMD FSR 4.1.1 (ML upscaler)");

	public final String label;
	public final String description;

	BackendId(String label, String description) {
		this.label = label;
		this.description = description;
	}
}

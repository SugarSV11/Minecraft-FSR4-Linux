package dev.fsr4linux.core;

import java.util.Locale;

/** Only a provider failure may trigger an automatic 4.1.1 -> 3.1.5 switch. */
public final class ProviderFallback {
	private ProviderFallback() { }
	public static boolean from411To315(String cause) {
		if (cause == null) return false;
		String s = cause.toLowerCase(Locale.ROOT);
		if (s.contains("timeout") || s.contains("stalled") || s.contains("helper died") || s.contains("not running")) return false;
		return s.contains("ffxcreatecontext") || s.contains("provider mismatch") || s.contains("not offered")
			|| s.contains("ffxdispatch rc=");
	}
}

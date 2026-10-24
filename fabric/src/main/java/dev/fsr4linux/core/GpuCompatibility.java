package dev.fsr4linux.core;

/** Vulkan facts for the device Minecraft actually selected. Provider queries and dispatch still decide support. */
public record GpuCompatibility(boolean linux, int vendorId, int deviceType, int apiMajor, int apiMinor,
	boolean externalMemory, boolean requiredFormats, int gfxIp) {
	public static final int AMD_VENDOR = 0x1002;
	public static final int INTEGRATED = 1, DISCRETE = 2; // VkPhysicalDeviceType values

	public boolean targetDevice() {
		return linux && vendorId == AMD_VENDOR && (deviceType == INTEGRATED || deviceType == DISCRETE);
	}

	public boolean bridgeCapable() {
		return targetDevice() && (apiMajor > 1 || apiMajor == 1 && apiMinor >= 3) && externalMemory && requiredFormats;
	}

	public boolean mayTryFsr315(boolean sdkOffers315) { return bridgeCapable() && sdkOffers315; }
	public boolean mayTryFsr411(boolean sdkOffers411) {
		boolean eligibleGfxIp = gfxIp == 0 || gfxIp >= 1100 && gfxIp < 1150 || gfxIp >= 1200 && gfxIp < 1300;
		return bridgeCapable() && deviceType == DISCRETE && eligibleGfxIp && sdkOffers411;
	}
	public boolean mayTryOwnFg() { return bridgeCapable(); }
	public boolean mayTryAmdFg() { return bridgeCapable() && deviceType == DISCRETE; }

	/** RADV's technical GFX target, when present; never a substitute for Vulkan capabilities or SDK results. */
	public static int gfxIpFromDriverName(String name) {
		if (name == null) return 0;
		java.util.regex.Matcher m = java.util.regex.Pattern.compile("(?i)\\bGFX(\\d{4})\\b").matcher(name);
		return m.find() ? Integer.parseInt(m.group(1)) : 0;
	}

	public String reason() {
		if (!linux) return "Linux required";
		if (vendorId != AMD_VENDOR) return "selected Vulkan GPU is not AMD Radeon";
		if (deviceType != INTEGRATED && deviceType != DISCRETE) return "selected Vulkan GPU is not a Radeon graphics device";
		if (apiMajor < 1 || apiMajor == 1 && apiMinor < 3) return "Vulkan 1.3 required";
		if (!externalMemory) return "Vulkan dma-buf external memory extensions missing";
		if (!requiredFormats) return "required sampled/storage image formats missing";
		return "";
	}
}

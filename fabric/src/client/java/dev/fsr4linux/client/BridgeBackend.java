package dev.fsr4linux.client;

import dev.fsr4linux.core.BackendException;
import dev.fsr4linux.core.BackendId;
import dev.fsr4linux.core.BackendState;
import dev.fsr4linux.core.BackendStatus;
import dev.fsr4linux.core.QualityMode;
import dev.fsr4linux.core.UpscalerBackend;
import java.util.EnumSet;
import java.util.Set;

/**
 * FSR 3.1.5 / FSR 4.1.1 through the Wine bridge. Both run the OFFICIAL AMD FidelityFX DLLs (DX12) in a helper process under
 * GE-Proton + VKD3D-Proton; Minecraft's Vulkan images are shared with it as dma-bufs (see docs/FSR4_EXPERIMENTS.md).
 * The "executed implementation" is only reported after the AMD runtime itself confirmed which provider created the context.
 */
public final class BridgeBackend implements UpscalerBackend {
	private final BackendId id;
	private final FsrRuntime rt = FsrRuntime.get();
	private BackendState state = BackendState.UNPROBED;
	private boolean initialised;

	public BridgeBackend(BackendId id) {
		if (id != BackendId.FSR3_1_5 && id != BackendId.FSR4_1_1) throw new IllegalArgumentException(id.name());
		this.id = id;
	}

	private String version() { return id.label.replace("FSR ", ""); }

	@Override public BackendId id() { return id; }

	@Override
	public BackendStatus probe() {
		String api = "DX12 (official AMD DLL) via Wine/VKD3D-Proton -> dma-buf -> Vulkan";
		if (!VulkanCaps.externalMemoryEnabled) return BackendStatus.unavailable(id, api, "Vulkan external memory extensions not enabled on Minecraft's device: " + VulkanCaps.reason);
		if (id == BackendId.FSR4_1_1 && !VulkanCaps.gpu.mayTryFsr411(true))
			return BackendStatus.unavailable(id, api, "FSR 4.1.1 requires a compatible discrete AMD Radeon"
				+ (VulkanCaps.gpu.reason().isEmpty() ? "" : "; " + VulkanCaps.gpu.reason()));
		if (id == BackendId.FSR3_1_5 && !VulkanCaps.gpu.mayTryFsr315(true))
			return BackendStatus.unavailable(id, api, VulkanCaps.gpu.reason());
		switch (rt.bridgeState) {
			case NOT_STARTED -> { return BackendStatus.unavailable(id, api, "bridge not started"); }
			case STARTING -> { return BackendStatus.unavailable(id, api, "bridge starting"); }
			case FAILED -> { return BackendStatus.unavailable(id, api, "bridge failed: " + rt.bridgeFailure); }
			default -> { }
		}
		if (!rt.bridgeAlive()) return BackendStatus.unavailable(id, api, "bridge process is not running");
		if (!rt.providerOffered(version())) return BackendStatus.unavailable(id, api, "AMD runtime does not offer provider " + version() + " (offered: " + rt.offeredProviders + "); try GE-Proton with VKD3D-Proton >= 3.0");
		if (state == BackendState.UNPROBED || state == BackendState.UNAVAILABLE) state = BackendState.AVAILABLE;
		return status();
	}

	@Override
	public BackendStatus status() {
		boolean proven = initialised && rt.lastExecutedProvider.equals(version());
		return new BackendStatus(id, state, version(), "DX12 via Wine/VKD3D-Proton, Vulkan dma-buf interop", "AMD Radeon (RADV)",
			state == BackendState.FAILED ? rt.lastError : null, proven ? "AMD FFX upscaler provider " + version() + " (reported by the AMD runtime)" : null);
	}

	@Override public Set<QualityMode> supportedQualities() { return EnumSet.allOf(QualityMode.class); }

	@Override
	public void initialize(int w, int h, QualityMode q) throws BackendException {
		state = BackendState.INITIALIZING;
		try {
			rt.allocate(id, w, h, q);
			initialised = true; state = BackendState.READY;
		} catch (BackendException e) {
			initialised = false; state = BackendState.FAILED; rt.lastError = e.getMessage(); throw e;
		}
	}

	@Override public void resize(int w, int h, QualityMode q) throws BackendException { initialize(w, h, q); }
	@Override public void resetHistory() { rt.resetHistory(); }

	@Override
	public void shutdown() {
		state = BackendState.DESTROYING;
		rt.releaseGpuNow();
		initialised = false; state = BackendState.AVAILABLE;
	}
}

package dev.fsr4linux.core;

import java.util.Set;

/**
 * One upscaling implementation. All methods are called on the render thread at a frame boundary, after the
 * controller has synchronised the GPU, so implementations may free/replace Vulkan resources freely.
 */
public interface UpscalerBackend {
	BackendId id();

	/** Cheap, cacheable availability check with a concrete reason when unavailable. */
	BackendStatus probe();

	/** Current status (state, device, executed implementation id). */
	BackendStatus status();

	Set<QualityMode> supportedQualities();

	/** Allocates all per-size resources. Throws with a concrete cause on failure; must leave nothing allocated. */
	void initialize(int outputWidth, int outputHeight, QualityMode quality) throws BackendException;

	/** Recreates resources for a new output size / quality. On failure the backend is left uninitialised. */
	void resize(int outputWidth, int outputHeight, QualityMode quality) throws BackendException;

	/** Discards temporal history (next dispatch is treated as a camera cut). */
	void resetHistory();

	/** Frees every resource. Idempotent. */
	void shutdown();
}

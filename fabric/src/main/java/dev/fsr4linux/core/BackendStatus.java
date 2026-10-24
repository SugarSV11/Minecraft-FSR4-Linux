package dev.fsr4linux.core;

/**
 * Honest report of a backend: availability, version, API, device, state, concrete failure cause and the
 * identifier of the implementation that really executed (null until a dispatch has actually run).
 */
public record BackendStatus(
	BackendId id,
	BackendState state,
	String version,
	String api,
	String device,
	String failureCause,
	String executedImplementationId
) {
	public boolean available() {
		return state == BackendState.AVAILABLE || state == BackendState.READY || state == BackendState.INITIALIZING;
	}

	public static BackendStatus unavailable(BackendId id, String api, String cause) {
		return new BackendStatus(id, BackendState.UNAVAILABLE, "-", api, "-", cause, null);
	}
}

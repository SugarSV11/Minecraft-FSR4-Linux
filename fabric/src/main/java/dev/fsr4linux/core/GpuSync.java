package dev.fsr4linux.core;

/** What the core needs from the renderer to replace GPU resources safely. */
public interface GpuSync {
	/** Blocks until no submitted GPU work references resources that are about to be replaced (e.g. vkDeviceWaitIdle). */
	void waitForGpuIdle();
}

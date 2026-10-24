package dev.fsr4linux.core;

public record Notification(String text, boolean error) {
	public static final long DURATION_MS = 2000L;
}

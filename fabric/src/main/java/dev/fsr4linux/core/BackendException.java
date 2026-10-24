package dev.fsr4linux.core;

public class BackendException extends Exception {
	public BackendException(String message) { super(message); }
	public BackendException(String message, Throwable cause) { super(message, cause); }
}

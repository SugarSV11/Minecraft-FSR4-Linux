package dev.fsr4linux.core;

/** Lifecycle of one backend instance. */
public enum BackendState { UNPROBED, UNAVAILABLE, AVAILABLE, INITIALIZING, READY, FAILED, DESTROYING }

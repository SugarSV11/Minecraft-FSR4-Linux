package dev.fsr4linux.core;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;

import java.io.IOException;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;

/** Persistent user settings (JSON). Unknown/invalid values fall back to defaults instead of failing startup. */
public final class Fsr4Config {
	private static final Gson GSON = new GsonBuilder().setPrettyPrinting().create();

	public boolean enabled = false;
	public String backend = BackendId.NATIVE.name();
	public String quality = QualityMode.QUALITY.name();
	public float sharpness = 0.5f;
	public boolean debugOverlay = false;
	public boolean fpsCounter = false;
	/** Frame Generation (FSR FG 4.0.1), independent of upscaling. Persisted as the user's intent; becomes active only when the bridge runs. */
	public boolean frameGeneration = false;
	/** Delay the real frame by half an interval after the generated one (even presentation): costs latency AND throughput on the single render thread. Off by default. */
	public boolean fgPacing = false;
	/** Frame-generation multiplier: 2 = AMD FSR FG 4.0.1 (official, default); 3 = EXPERIMENTAL own backend that presents two generated frames per rendered frame, 4 = EXPERIMENTAL own backend with three (no AMD code involved). */
	public int fgMultiplier = 2;

	public BackendId backendId() {
		try { return BackendId.valueOf(backend); } catch (RuntimeException e) { return BackendId.NATIVE; }
	}

	public QualityMode qualityMode() {
		try { return QualityMode.valueOf(quality); } catch (RuntimeException e) { return QualityMode.QUALITY; }
	}

	public static Fsr4Config load(Path file) {
		try {
			if (Files.exists(file)) {
				Fsr4Config c = GSON.fromJson(Files.readString(file), Fsr4Config.class);
				if (c != null) { c.sharpness = Math.max(0f, Math.min(1f, c.sharpness)); if (c.fgMultiplier != 3 && c.fgMultiplier != 4) c.fgMultiplier = 2; return c; }
			}
		} catch (IOException | RuntimeException ignored) {
			// corrupt or unreadable: keep defaults; the next save overwrites it
		}
		return new Fsr4Config();
	}

	/** Atomic write so a crash mid-save never leaves a truncated file. */
	public void save(Path file) throws IOException {
		Path tmp = file.resolveSibling(file.getFileName() + ".tmp");
		Files.createDirectories(file.toAbsolutePath().getParent());
		Files.writeString(tmp, GSON.toJson(this));
		try {
			Files.move(tmp, file, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
		} catch (AtomicMoveNotSupportedException e) {
			Files.move(tmp, file, StandardCopyOption.REPLACE_EXISTING);
		}
	}
}

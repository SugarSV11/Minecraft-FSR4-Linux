package dev.fsr4linux.client;

import dev.fsr4linux.core.Notification;

import java.io.IOException;
import java.io.InputStream;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.Map;
import java.util.HashMap;
import java.util.Set;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.jar.JarFile;

/**
 * Auto-bootstrap system for FSR4-Linux runtime dependencies.
 * Extracts bundled native libraries and AMD DLLs, downloads GE-Proton if needed,
 * and prepares the runtime directory on first launch.
 */
public final class FsrBootstrap {
    private static final FsrBootstrap INSTANCE = new FsrBootstrap();
    public static FsrBootstrap get() { return INSTANCE; }

    // The release build generates SHA256SUMS from the exact bytes packaged in the JAR.
    private record Resource(String jarPath, String destSubPath) {}
    private static final String RESOURCE_PREFIX = "dev/fsr4linux/bootstrap/";

    // Our own binaries (MIT license - we own them)
    private static final Resource[] OUR_RESOURCES = {
        new Resource(RESOURCE_PREFIX + "libfsr4linux.so", "lib/libfsr4linux.so"),
        new Resource(RESOURCE_PREFIX + "fsr4_bridge.exe", "bridge/fsr4_bridge.exe"),
    };

    // AMD FidelityFX DLLs (MIT-like license from SDK v2.3.0 - redistributable per license)
    private static final Resource[] AMD_DLLS = {
        new Resource(RESOURCE_PREFIX + "amd_fidelityfx_loader_dx12.dll", "bridge/amd_fidelityfx_loader_dx12.dll"),
        new Resource(RESOURCE_PREFIX + "amd_fidelityfx_upscaler_dx12.dll", "bridge/amd_fidelityfx_upscaler_dx12.dll"),
        new Resource(RESOURCE_PREFIX + "amd_fidelityfx_framegeneration_dx12.dll", "bridge/amd_fidelityfx_framegeneration_dx12.dll"),
    };

    // License file
    private static final Resource LICENSE_RESOURCE = new Resource(
        RESOURCE_PREFIX + "AMD_FIDELITYFX_LICENSE.md", "bridge/AMD_FIDELITYFX_LICENSE.md");

    // Proton wrapper script (our own, MIT license)
    private static final Resource PROTON_WRAPPER = new Resource(
        RESOURCE_PREFIX + "proton-wrapper.py", "proton/proton");

    // GE-Proton configuration (LGPL - auto-download from official releases)
    private static final String GE_PROTON_VERSION = "GE-Proton11-11";
    private static final String GE_PROTON_URL = "https://github.com/GloriousEggroll/proton-ge-custom/releases/download/" + GE_PROTON_VERSION + "/" + GE_PROTON_VERSION + "-x86_64.tar.gz";
    private static final String GE_PROTON_SHA512 = "45721a7b625b2404d8896bb5e2a8b87fc9623a394873a6dd6b900c839681d5c01ec694ca00d003fc9c733c5f6c5648af8171985656f9fa47e20999fbebdbe235";
    private static final String GE_PROTON_DIR_NAME = GE_PROTON_VERSION;

    // State
    private Path runtimeDir;
    private volatile BootstrapState state = BootstrapState.NOT_STARTED;
    private volatile String errorMessage = "";
    private final ExecutorService executor = Executors.newSingleThreadExecutor(r -> { Thread t = new Thread(r, "fsr4linux-bootstrap"); t.setDaemon(true); return t; });
    private CompletableFuture<Void> bootstrapFuture;

    public enum BootstrapState { NOT_STARTED, EXTRACTING, DOWNLOADING_PROTON, VERIFYING, COMPLETE, FAILED }

    private FsrBootstrap() {}

    /**
     * Starts the bootstrap process asynchronously. Safe to call multiple times.
     * @param configDir Fabric config directory (used to derive runtime dir)
     * @param notify Callback for user notifications
     */
    public void startAsync(Path configDir, java.util.function.Consumer<Notification> notify) {
        if (state != BootstrapState.NOT_STARTED) return;
        runtimeDir = configDir.resolve("fsr4linux-runtime");
        state = BootstrapState.EXTRACTING;
        notify.accept(new Notification("FSR4: Preparando runtime...", false));

        bootstrapFuture = CompletableFuture.runAsync(() -> {
            try {
                extractBundledResources();
                state = BootstrapState.DOWNLOADING_PROTON;
                notify.accept(new Notification("FSR4: Descargando GE-Proton...", false));
                downloadAndExtractProton();
                state = BootstrapState.VERIFYING;
                verifyInstallation();
                state = BootstrapState.COMPLETE;
                notify.accept(new Notification("FSR4: Runtime listo ✓", false));
                Fsr4LinuxClient.LOGGER.info("FSR4 bootstrap completed successfully at {}", runtimeDir);
            } catch (Throwable e) {
                state = BootstrapState.FAILED;
                errorMessage = e.getMessage();
                Fsr4LinuxClient.LOGGER.error("FSR4 bootstrap failed", e);
                notify.accept(new Notification("FSR4: Error en bootstrap: " + errorMessage, true));
            }
        }, executor);
    }

    public boolean isComplete() { return state == BootstrapState.COMPLETE; }
    public boolean isFailed() { return state == BootstrapState.FAILED; }
    public BootstrapState getState() { return state; }
    public String getError() { return errorMessage; }
    public Path getRuntimeDir() { return runtimeDir; }
    public Path getLibDir() { return runtimeDir.resolve("lib"); }
    public Path getBridgeDir() { return runtimeDir.resolve("bridge"); }
    public Path getPrefixDir() { return runtimeDir.resolve("prefix"); }
    public Path getProtonDir() { return runtimeDir.resolve("proton").resolve(GE_PROTON_DIR_NAME); }

    /** Waits for bootstrap to complete (for testing). */
    public void join() {
        if (bootstrapFuture != null) bootstrapFuture.join();
    }

    private void extractBundledResources() throws IOException {
        Map<String, String> hashes = readResourceManifest();
        // Extract our binaries
        for (Resource res : OUR_RESOURCES) extractResource(res, hashes);
        // Extract AMD DLLs
        for (Resource res : AMD_DLLS) extractResource(res, hashes);
        // Extract license
        extractResource(LICENSE_RESOURCE, hashes);
        // Extract proton wrapper script
        extractResource(PROTON_WRAPPER, hashes);

        // Create prefix directory structure
        Files.createDirectories(getPrefixDir().resolve("pfx/drive_c/windows/system32"));
        installLocalAmdDriver();
    }

    /** Reuse an AMD driver component already installed by Proton on this machine. */
    private void installLocalAmdDriver() throws IOException {
        Path target = getPrefixDir().resolve("pfx/drive_c/windows/system32/amdxcffx64.dll");
        if (isAmdDriverDll(target)) return;
        Path home = Path.of(System.getProperty("user.home"));
        Path[] candidates = {
            home.resolve(".local/share/Steam/steamapps/common/Proton - Experimental/contrib/amdxcffx64.dll"),
            home.resolve(".local/share/fsr4linux/prefix/pfx/drive_c/windows/system32/amdxcffx64.dll"),
            home.resolve(".cache/fsr4-linux/pfx/pfx/drive_c/windows/system32/amdxcffx64.dll"),
            home.resolve(".cache/fsr4-linux/pfx_ge/pfx/drive_c/windows/system32/amdxcffx64.dll")
        };
        for (Path source : candidates) {
            if (!isAmdDriverDll(source)) continue;
            try {
                Path temp = Files.createTempFile(target.getParent(), ".amdxcffx64-", ".tmp");
                try {
                    Files.copy(source, temp, StandardCopyOption.REPLACE_EXISTING);
                    if (!isAmdDriverDll(temp) || !sha256(temp).equals(sha256(source)))
                        throw new IOException("AMD driver component changed during copy: " + source);
                    Files.move(temp, target, StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
                } finally { Files.deleteIfExists(temp); }
                Fsr4LinuxClient.LOGGER.info("FSR4: reused locally installed AMD driver component (SHA-256 {})", sha256(target));
                return;
            } catch (IOException e) {
                Fsr4LinuxClient.LOGGER.warn("FSR4: could not reuse AMD driver component from {}: {}", source, e.toString());
            }
        }
        if (Files.exists(target)) {
            Path backup = target.resolveSibling("amdxcffx64.invalid-" + System.currentTimeMillis() + ".dll");
            Files.move(target, backup, StandardCopyOption.ATOMIC_MOVE);
            Fsr4LinuxClient.LOGGER.warn("FSR4: moved invalid AMD driver component to {}", backup);
        }
        Fsr4LinuxClient.LOGGER.info("FSR4: no local AMD driver component found; GE-Proton will try its verified FSR4 upgrade");
    }

    private static boolean isAmdDriverDll(Path path) {
        try {
            if (!Files.isRegularFile(path) || Files.size(path) < 1_000_000) return false;
            byte[] header;
            try (var in = Files.newInputStream(path)) { header = in.readNBytes(512); }
            if (header.length < 64 || header[0] != 'M' || header[1] != 'Z') return false;
            int pe = (header[0x3c] & 255) | ((header[0x3d] & 255) << 8)
                | ((header[0x3e] & 255) << 16) | ((header[0x3f] & 255) << 24);
            if (pe < 64 || pe > 4096) return false;
            try (var in = Files.newInputStream(path)) {
                in.skipNBytes(pe);
                byte[] signature = in.readNBytes(6);
                return signature.length == 6 && signature[0] == 'P' && signature[1] == 'E'
                    && signature[2] == 0 && signature[3] == 0
                    && (signature[4] & 255) == 0x64 && (signature[5] & 255) == 0x86;
            }
        } catch (IOException e) { return false; }
    }

    private Map<String, String> readResourceManifest() throws IOException {
        Map<String, String> hashes = new HashMap<>();
        try (InputStream in = getClass().getClassLoader().getResourceAsStream(RESOURCE_PREFIX + "SHA256SUMS")) {
            if (in == null) throw new IOException("Bootstrap SHA256SUMS missing from JAR");
            for (String line : new String(in.readAllBytes(), java.nio.charset.StandardCharsets.US_ASCII).split("\\R")) {
                if (!line.matches("[0-9a-f]{64}  [^/\\\\]+")) throw new IOException("Invalid bootstrap manifest entry: " + line);
                String name = line.substring(66);
                if (hashes.putIfAbsent(RESOURCE_PREFIX + name, line.substring(0, 64)) != null)
                    throw new IOException("Duplicate bootstrap manifest entry: " + name);
            }
        }
        Set<String> expected = new java.util.HashSet<>();
        for (Resource r : OUR_RESOURCES) expected.add(r.jarPath);
        for (Resource r : AMD_DLLS) expected.add(r.jarPath);
        expected.add(LICENSE_RESOURCE.jarPath);
        expected.add(PROTON_WRAPPER.jarPath);
        if (!hashes.keySet().equals(expected)) throw new IOException("Bootstrap manifest resource set mismatch");
        return hashes;
    }

    private void extractResource(Resource res, Map<String, String> hashes) throws IOException {
        Path dest = runtimeDir.resolve(res.destSubPath);
        Files.createDirectories(dest.getParent());
        String expected = hashes.get(res.jarPath);
        if (Files.isRegularFile(dest) && sha256(dest).equals(expected)) return;
        Path temp = Files.createTempFile(dest.getParent(), ".fsr4-", ".tmp");
        Path backup = dest.resolveSibling(dest.getFileName() + ".bak");
        Path backupTemp = null;
        try {
            try (InputStream in = getClass().getClassLoader().getResourceAsStream(res.jarPath)) {
                if (in == null) throw new IOException("Resource not found in JAR: " + res.jarPath);
                Files.copy(in, temp, StandardCopyOption.REPLACE_EXISTING);
            }
            String actual = sha256(temp);
            if (!actual.equals(expected)) throw new IOException("SHA-256 mismatch for " + res.destSubPath + ": expected " + expected + " got " + actual);
            if (res.destSubPath.endsWith(".so") || res.destSubPath.endsWith(".exe") || res.destSubPath.equals("proton/proton"))
                if (!temp.toFile().setExecutable(true)) throw new IOException("Cannot make resource executable: " + res.destSubPath);
            if (Files.exists(dest)) {
                backupTemp = Files.createTempFile(dest.getParent(), ".fsr4-backup-", ".tmp");
                Files.copy(dest, backupTemp, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.COPY_ATTRIBUTES);
                atomicMove(backupTemp, backup);
            }
            atomicMove(temp, dest);
        } finally {
            Files.deleteIfExists(temp);
            if (backupTemp != null) Files.deleteIfExists(backupTemp);
        }

        // Make executables executable
        Fsr4LinuxClient.LOGGER.info("FSR4 extracted: {} (verified)", res.destSubPath);
    }

    private static void atomicMove(Path from, Path to) throws IOException {
        Files.move(from, to, StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING);
    }

    private void downloadAndExtractProton() throws IOException, InterruptedException {
        Path protonRoot = runtimeDir.resolve("proton");
        Path protonDir = getProtonDir();
        Path marker = protonRoot.resolve(".installed-" + GE_PROTON_VERSION);

        // Already installed?
        if (Files.exists(marker) && Files.isDirectory(protonDir) && Files.exists(protonDir.resolve("proton"))) {
            Fsr4LinuxClient.LOGGER.info("GE-Proton already installed at {}", protonDir);
            return;
        }

        // Download with retries
        Path archive = protonRoot.resolve(GE_PROTON_VERSION + ".tar.gz");
        Files.createDirectories(protonRoot);

        int maxRetries = 3;
        int attempt = 0;
        Exception lastException = null;

        while (attempt < maxRetries) {
            attempt++;
            Fsr4LinuxClient.LOGGER.info("Downloading GE-Proton (attempt {}/{})...", attempt, maxRetries);
            try {
                HttpClient client = HttpClient.newBuilder()
                    .followRedirects(HttpClient.Redirect.ALWAYS)
                    .build();
                HttpRequest request = HttpRequest.newBuilder(URI.create(GE_PROTON_URL))
                    .header("User-Agent", "fsr4linux-bootstrap/1.0")
                    .timeout(java.time.Duration.ofMinutes(30))
                    .GET().build();

                HttpResponse<Path> response = client.send(request, HttpResponse.BodyHandlers.ofFile(archive));
                if (response.statusCode() != 200) {
                    throw new IOException("Failed to download GE-Proton: HTTP " + response.statusCode());
                }

                // Verify SHA-512
                Fsr4LinuxClient.LOGGER.info("Verifying GE-Proton SHA-512...");
                String actualSha512 = sha512(archive);
                if (!actualSha512.equalsIgnoreCase(GE_PROTON_SHA512)) {
                    Files.deleteIfExists(archive);
                    throw new IOException("GE-Proton SHA-512 mismatch: expected " + GE_PROTON_SHA512 + " got " + actualSha512);
                }
                Fsr4LinuxClient.LOGGER.info("SHA-512 verified OK");

                // Extract tar.gz using system tar command (handles GNU tar, PAX headers, etc.)
                Fsr4LinuxClient.LOGGER.info("Extracting GE-Proton...");
                ProcessBuilder pb = new ProcessBuilder("tar", "-xzf", archive.toString(), "-C", protonRoot.toString());
                pb.redirectErrorStream(true);
                Process process = pb.start();
                int exitCode = process.waitFor();
                if (exitCode != 0) {
                    String output = new String(process.getInputStream().readAllBytes());
                    throw new IOException("tar extraction failed with exit code " + exitCode + ": " + output);
                }

                // Find the extracted GE-Proton directory (should be the only directory in protonRoot)
                Path extractedDir = null;
                try (var stream = Files.list(protonRoot)) {
                    for (Path p : stream.toList()) {
                        if (Files.isDirectory(p) && p.getFileName().toString().startsWith("GE-Proton")) {
                            extractedDir = p;
                            break;
                        }
                    }
                }
                if (extractedDir == null) {
                    throw new IOException("GE-Proton directory not found after extraction");
                }

                // The extracted dir should be GE-Proton11-11-x86_64 or similar; we want to use it directly
                // as the proton dir. Rename it to our standard name if needed.
                if (!extractedDir.equals(protonDir)) {
                    if (Files.exists(protonDir)) {
                        // Clean up existing
                        Files.walk(protonDir)
                            .sorted(java.util.Comparator.reverseOrder())
                            .forEach(path -> {
                                try { Files.delete(path); } catch (IOException ignored) {}
                            });
                    }
                    Files.move(extractedDir, protonDir);
                }

                // Clean up archive and mark installed
                Files.deleteIfExists(archive);
                Files.writeString(marker, "ok");
                Fsr4LinuxClient.LOGGER.info("GE-Proton installed at {}", protonDir);
                return; // Success!

            } catch (Exception e) {
                lastException = e;
                Fsr4LinuxClient.LOGGER.warn("GE-Proton download/extract failed (attempt {}/{}): {}", attempt, maxRetries, e.getMessage());
                Files.deleteIfExists(archive); // Clean up partial download
                if (attempt < maxRetries) {
                    Thread.sleep(5000 * attempt); // Exponential backoff
                }
            }
        }

        throw new IOException("GE-Proton download failed after " + maxRetries + " attempts: " + lastException.getMessage(), lastException);
    }

    private void verifyInstallation() throws IOException {
        List<String> missing = new ArrayList<>();

        // Check our resources
        for (Resource res : OUR_RESOURCES) {
            Path p = runtimeDir.resolve(res.destSubPath);
            if (!Files.isRegularFile(p)) missing.add(res.destSubPath);
        }
        // Check AMD DLLs
        for (Resource res : AMD_DLLS) {
            Path p = runtimeDir.resolve(res.destSubPath);
            if (!Files.isRegularFile(p)) missing.add(res.destSubPath);
        }
        // Check GE-Proton
        if (!Files.exists(getProtonDir().resolve("proton"))) {
            missing.add("proton/" + GE_PROTON_DIR_NAME + "/proton");
        }

        if (!missing.isEmpty()) {
            throw new IOException("Missing files after bootstrap: " + String.join(", ", missing));
        }
    }

    private static String sha256(Path file) throws IOException {
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            byte[] buf = new byte[8192];
            int n;
            try (var in = Files.newInputStream(file)) {
                while ((n = in.read(buf)) > 0) md.update(buf, 0, n);
            }
            return bytesToHex(md.digest());
        } catch (NoSuchAlgorithmException e) {
            throw new IOException("SHA-256 not available", e);
        }
    }

    private static String sha512(Path file) throws IOException {
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-512");
            byte[] buf = new byte[8192];
            int n;
            try (var in = Files.newInputStream(file)) {
                while ((n = in.read(buf)) > 0) md.update(buf, 0, n);
            }
            return bytesToHex(md.digest());
        } catch (NoSuchAlgorithmException e) {
            throw new IOException("SHA-512 not available", e);
        }
    }

    private static String bytesToHex(byte[] bytes) {
        StringBuilder sb = new StringBuilder(bytes.length * 2);
        for (byte b : bytes) sb.append(String.format("%02x", b));
        return sb.toString();
    }

    public void shutdown() {
        executor.shutdownNow();
    }
}

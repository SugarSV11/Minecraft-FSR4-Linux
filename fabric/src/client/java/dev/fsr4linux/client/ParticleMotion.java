package dev.fsr4linux.client;

import java.util.IdentityHashMap;
import net.minecraft.client.particle.SingleQuadParticle;

/**
 * Collects the quad particles of the frame being extracted (Minecraft's extract phase precedes the render phase of the same frame) so the prep pass can give them their own
 * depth and motion vector. Particles are camera-facing quads: centre (camera-relative, interpolated), half size and the MEASURED displacement of their interpolated world position since the
 * previous rendered frame (session 15: replaces velocity x wall-clock time, which was wrong for any tick rate other than 20, pause, /tick freeze and long frames).
 * Opt-in via FSR4_PARTICLE_MV=1 until measured (see docs/PARTICLE_MV.md).
 */
public final class ParticleMotion {
	public static final boolean ENABLED = "1".equals(System.getenv("FSR4_PARTICLE_MV"));
	public static final int MAX = 4096, FLOATS = 8;
	private static final float[] raw = new float[MAX * 6];   // x y z (camera-relative) vdx dy dz (blocks moved since the previous frame) ; half size kept separately
	private static final float[] size = new float[MAX];
	private static int n;
	/** Absolute interpolated position of every particle in the previous / current frame (identity keyed): the displacement is measured, not predicted from velocity x time. */
	private static IdentityHashMap<SingleQuadParticle, double[]> prevPos = new IdentityHashMap<>(), curPos = new IdentityHashMap<>();
	public static int lastCount, lastKept;

	private ParticleMotion() { }


	/** Start of a frame's particle extraction. */
	public static void begin() { n = 0; var t = prevPos; prevPos = curPos; curPos = t; curPos.clear(); }

	public static void add(SingleQuadParticle p, float pt, float x, float y, float z) {
		if (!ENABLED || n >= MAX || p.alpha < 0.04f) return;
		double ax = p.xo + (p.x - p.xo) * pt, ay = p.yo + (p.y - p.yo) * pt, az = p.zo + (p.z - p.zo) * pt;
		double[] before = prevPos.get(p); curPos.put(p, new double[] { ax, ay, az });
		int o = n * 6; raw[o] = x; raw[o + 1] = y; raw[o + 2] = z;
		// displacement since the previous rendered frame (0 for a particle first seen now): exact for any tick rate, pause, /tick freeze, lag spike, gravity or friction
		raw[o + 3] = before == null ? 0f : (float) (ax - before[0]); raw[o + 4] = before == null ? 0f : (float) (ay - before[1]); raw[o + 5] = before == null ? 0f : (float) (az - before[2]);
		size[n] = p.getQuadSize(pt); n++;
	}

	/**
	 * Fills {@code out} (8 floats per particle: centre, half extent X px, displacement, half extent Y px) for the prep pass; returns the count.
	 * view: forward vector (camera space -Z in world axes), projection scales m00/m11 and the render size in pixels.
	 */
	public static int build(float[] out, float fx, float fy, float fz, float m00, float m11, int w, int h) {
		int kept = 0; lastCount = n;
		for (int i = 0; i < n && kept < MAX; i++) {
			int o = i * 6; float x = raw[o], y = raw[o + 1], z = raw[o + 2];
			float depth = x * fx + y * fy + z * fz;   // distance along the view direction
			if (depth < 0.1f) continue;
			float hx = size[i] * m00 / depth * 0.5f * w, hy = size[i] * m11 / depth * 0.5f * h;
			if (hx < 0.4f && hy < 0.4f) continue;   // sub-pixel
			int q = kept * FLOATS;
			out[q] = x; out[q + 1] = y; out[q + 2] = z; out[q + 3] = hx;
			out[q + 4] = raw[o + 3]; out[q + 5] = raw[o + 4]; out[q + 6] = raw[o + 5]; out[q + 7] = hy; kept++;
		}
		lastKept = kept; return kept;
	}
}

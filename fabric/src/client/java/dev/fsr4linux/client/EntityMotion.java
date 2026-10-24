package dev.fsr4linux.client;

import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;
import net.minecraft.client.Minecraft;
import net.minecraft.client.multiplayer.ClientLevel;
import net.minecraft.util.Mth;
import net.minecraft.world.entity.Entity;
import net.minecraft.world.entity.LivingEntity;
import net.minecraft.world.phys.Vec3;

/**
 * Per-entity RIGID-BODY motion for the prep pass. Each rendered frame it records every entity's interpolated render transform (position, body yaw) and, next frame,
 * hands the nearest {@link #MAX} entities to the GPU as (pivot, yaw delta, bounding box, displacement). Honest scope: translation + yaw only, classified by bounding box;
 * skeletal animation (limbs/head/tail) is NOT captured and the box also claims background pixels that fall inside it (the depth-reconstructed point must lie inside the box).
 * History is dropped on level change (dimension), on teleports and for newly appearing entities (zero motion = treated as static).
 */
public final class EntityMotion {
	public static final int MAX = 48, FLOATS = 16;
	private static final float BOX_PAD = Float.parseFloat(System.getenv().getOrDefault("FSR4_ENTITY_PAD", "0.3"));   // horizontal inflation of the logical box (models overhang it)
	private static final double TELEPORT = 6.0;   // blocks per frame above which an entity is treated as newly appeared
	private final Map<Integer, double[]> prev = new HashMap<>();   // id -> x,y,z,yaw (interpolated, previous rendered frame)
	private final Map<Integer, double[]> cur = new HashMap<>();
	private ClientLevel level;
	public final float[] data = new float[MAX * FLOATS];
	public int count, moving, tracked;
	private static final boolean DEBUG = System.getenv("FSR4_ENTITY_DEBUG") != null; private int dbgN;

	public void reset() { prev.clear(); cur.clear(); count = 0; }

	/** Builds the entity list for this frame (camera-relative current transforms, displacement since the previous rendered frame). */
	public void update(Minecraft mc, float pt, Vec3 camPos, Entity cameraEntity, boolean firstPerson) {
		if (mc.level != level) { reset(); level = mc.level; }
		if (level == null) { count = 0; return; }
		cur.clear();
		moving = 0;
		record Item(double dist, float[] f) { }
		java.util.ArrayList<Item> items = new java.util.ArrayList<>();
		for (Entity e : level.entitiesForRendering()) {
			Vec3 p = e.getPosition(pt);
			float yaw = e instanceof LivingEntity le ? Mth.rotLerp(pt, le.yBodyRotO, le.yBodyRot) : Mth.rotLerp(pt, e.yRotO, e.getYRot());
			cur.put(e.getId(), new double[]{p.x, p.y, p.z, yaw});
			if (firstPerson && e == cameraEntity) continue;   // never rendered as a body in first person
			if (e.isInvisible()) continue;
			double[] o = prev.get(e.getId());
			double dx = 0, dy = 0, dz = 0, dyaw = 0;
			if (o != null) {
				dx = p.x - o[0]; dy = p.y - o[1]; dz = p.z - o[2];
				if (dx * dx + dy * dy + dz * dz > TELEPORT * TELEPORT) { dx = dy = dz = 0; }   // teleport / re-spawn: no history
				else dyaw = Mth.wrapDegrees(yaw - o[3]);
			}
			float w = e.getBbWidth() * 0.5f + BOX_PAD, h = e.getBbHeight() * 0.5f;
			float[] f = new float[FLOATS];
			f[0] = (float) (p.x - camPos.x); f[1] = (float) (p.y - camPos.y); f[2] = (float) (p.z - camPos.z); f[3] = (float) Math.toRadians(dyaw);
			f[4] = 0; f[5] = h + 0.04f; f[6] = 0;
			f[8] = w; f[9] = h + 0.02f; f[10] = w;   // box spans feet+2 cm .. top+6 cm: the ground under the feet is not claimed, rendered models overhang the logical box
			f[12] = (float) dx; f[13] = (float) dy; f[14] = (float) dz;
			if (dx != 0 || dy != 0 || dz != 0 || dyaw != 0) moving++;
			if (DEBUG && e.getClass().getSimpleName().equals("Sheep")) Fsr4LinuxClient.LOGGER.info("SHEEP pt={} x={} xo={} xOld={} pos.x={} rend.x={}", pt, e.getX(), e.xo, e.xOld, p.x, Mth.lerp(pt, e.xOld, e.getX()));
			items.add(new Item(p.distanceToSqr(camPos), f));
		}
		items.sort(java.util.Comparator.comparingDouble(Item::dist));
		count = Math.min(MAX, items.size()); tracked = items.size();
		for (int i = 0; i < count; i++) System.arraycopy(items.get(i).f(), 0, data, i * FLOATS, FLOATS);
		if (DEBUG && (dbgN++ % 20) == 0 && true) Fsr4LinuxClient.LOGGER.info("EntityMotion tracked={} sent={} moving={} e0={}", tracked, count, moving, count > 0 ? Arrays.toString(Arrays.copyOf(data, FLOATS)) : "-");
		prev.clear(); prev.putAll(cur);
	}
}

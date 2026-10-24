package com.voxon.capturerig;

import net.minecraft.world.phys.Vec3;

/** Where each camera goes, and its lens. PORTED term for term from the Unity rig's Geometry (rig/unity), which is the
 * add-on's Derive, FrameFor and DepthPlanes (addon/capture_gpu.hpp): the decode host decodes these cameras with those
 * same formulas, so a difference here is a systematic offset of the whole volume.
 *
 * <p>The formulas are in the rig's own frame, Unreal's axes (X and Y level, Z up), which is left-handed. Minecraft's
 * world (X east, Y up, Z south) is right-handed, so a vector goes across as (x, y, z) to (x, z, y): a reflection,
 * which keeps each picture the right way round - a camera's right in the rig's frame is its right in the game's.</p> */
final class Geometry {
	float s, r, z, ex, ey, ez, ct, st, topDist, nTop, tanP;
	final float[] halfDepth = new float[4], vertHalf = new float[4], halfWin = new float[4], sideDist = new float[4], nSide = new float[4];
	int sideH;
	boolean persp;

	/** A face's camera, in the game's world: where it stands, its turn (Minecraft's yaw and pitch), its vertical lens
	 * and its clip planes. */
	record Pose(Vec3 pos, float yaw, float pitch, float fovV, float near, float far) { }

	static Geometry derive(RigSettings c) {
		Geometry g = new Geometry();
		g.s = c.radiusM / c.boundR;
		g.r = c.boundR * g.s;
		g.z = c.boundZ * g.s;
		g.persp = c.perspective;
		g.ex = g.r * (g.persp ? c.scaleX : 1f);
		g.ey = g.r * (g.persp ? c.scaleY : 1f);
		g.ez = g.z * (g.persp ? c.scaleZ : 1f);
		float tilt = (float)Math.toRadians(Math.max(0f, Math.min(90f, c.tiltDeg)));
		g.ct = (float)Math.cos(tilt);
		g.st = (float)Math.sin(tilt);
		g.sideH = sideHeight(c);
		float k = Math.max(1f, c.camDist);
		if (g.persp) g.tanP = 1f / k;
		for (int v = 0; v < 4; v++) {
			float along = v % 2 == 0 ? g.ex : g.ey, across = v % 2 == 0 ? g.ey : g.ex;
			g.halfDepth[v] = along * g.ct + g.ez * g.st;
			g.vertHalf[v] = along * g.st + g.ez * g.ct;
			g.halfWin[v] = Math.max(across, g.vertHalf[v] * c.faceRes / g.sideH);
			g.sideDist[v] = g.halfDepth[v] * c.camDist;
			if (g.persp) {
				g.nSide[v] = g.halfWin[v] * k;
				g.sideDist[v] = g.halfDepth[v] + g.nSide[v];
			}
		}
		g.topDist = g.ez * c.camDist;
		if (g.persp) {
			g.nTop = Math.max(g.ex, g.ey) * k;
			g.topDist = g.ez + g.nTop;
		}
		return g;
	}

	/** A side face's height: half its width on a display at least twice as wide as tall (VX2-XL), square on one as tall
	 * as wide (VX2). capture_gpu.hpp's SideHeight. */
	static int sideHeight(RigSettings c) { return 2f * c.boundZ <= c.boundR * 1.001f ? c.faceRes / 2 : c.faceRes; }

	static int faceHeight(int face, RigSettings c) { return face >= 4 ? c.faceRes : sideHeight(c); }

	/** capture_gpu.hpp's Reach: how far past the centre a face's camera may see and still be in the box. */
	private float reach(int face) {
		float slab = face < 4 ? halfDepth[face] : Math.max(halfDepth[0], halfDepth[1]);
		return Math.max(Math.max(ex, ey), Math.max(ez, slab));
	}

	/** The volume's centre in the game's world: the feet, less where they stand in the display (+Z down) - the decode's
	 * mapping undone: the display's turn back into the rig's frame, each axis's scale, then the rig's yaw. The Unity
	 * rig's Capture, term for term. */
	Vec3 centre(RigSettings c, Vec3 feet, double rigYawDeg, double dispTurnDeg) {
		double tA = Math.toRadians(dispTurnDeg), yA = Math.toRadians(rigYawDeg);
		double u = (c.feetX * Math.cos(tA) + c.feetY * Math.sin(tA)) * (ex / r);
		double w = (-c.feetX * Math.sin(tA) + c.feetY * Math.cos(tA)) * (ey / r);
		double ox = (u * Math.cos(yA) - w * Math.sin(yA)) * s, oy = (u * Math.sin(yA) + w * Math.cos(yA)) * s;
		return feet.subtract(toGame(ox, oy, 0)).add(0, c.feetZ * s * (ez / z), 0);
	}

	/** A face's camera: the Unity rig's Place (the add-on's placeFace) in the rig's frame, then into the game's. */
	Pose place(int face, RigSettings c, Vec3 centre, double rigYawDeg) {
		double pitch, yaw, px, py, pz;
		if (face >= 4) {
			double sign = face == 4 ? 1 : -1;
			px = 0;
			py = 0;
			pz = sign * topDist;
			pitch = -90 * sign;
			yaw = 90 * sign + rigYawDeg;
		} else {
			double[][] l = { { -1, 0 }, { 0, -1 }, { 1, 0 }, { 0, 1 } };
			double a = Math.toRadians(rigYawDeg), ca = Math.cos(a), sa = Math.sin(a);
			double bx = l[face][0] * ca - l[face][1] * sa, by = l[face][0] * sa + l[face][1] * ca;
			double lx = bx * ct, ly = by * ct, lz = -st;
			px = -lx * sideDist[face];
			py = -ly * sideDist[face];
			pz = -lz * sideDist[face];
			pitch = Math.toDegrees(Math.asin(lz));
			yaw = Math.toDegrees(Math.atan2(ly, lx));
		}
		double p = Math.toRadians(pitch), y = Math.toRadians(yaw);
		Vec3 fwd = toGame(Math.cos(p) * Math.cos(y), Math.cos(p) * Math.sin(y), Math.sin(p));
		Vec3 up = toGame(-Math.sin(p) * Math.cos(y), -Math.sin(p) * Math.sin(y), Math.cos(p));

		// The game's camera turn for that look: its pitch is down-positive, its yaw 0 faces +Z (south), 90 faces -X;
		// looking straight down (up) its picture's top is the way it faces (faces away from).
		float mcPitch = (float)Math.toDegrees(-Math.asin(Math.max(-1, Math.min(1, fwd.y))));
		double mcYaw;
		if (Math.abs(fwd.y) < 0.999) mcYaw = Math.atan2(-fwd.x, fwd.z);
		else if (fwd.y < 0) mcYaw = Math.atan2(-up.x, up.z);
		else mcYaw = Math.atan2(up.x, -up.z);

		int w = c.faceRes, h = faceHeight(face, c);
		float camD = face >= 4 ? topDist : sideDist[face];
		float near = persp ? (face >= 4 ? nTop : nSide[face]) * 0.98f : 0.05f;
		float far = (camD + 2f * reach(face)) * 1.5f;
		float tanV = tanP * h / w;          // side faces are half height; the lens spans the volume across
		float fovV = (float)Math.toDegrees(2 * Math.atan(tanV));
		return new Pose(centre.add(toGame(px, py, pz)), (float)Math.toDegrees(mcYaw), mcPitch, fovV, near, far);
	}

	/** The rig's frame (Unreal axes: X, Y level, Z up) to the game's (Y up): (x, y, z) to (x, z, y). */
	static Vec3 toGame(double x, double y, double z) { return new Vec3(x, z, y); }

	/** The rig's yaw of a look in the game's world: from the rig's +X toward its +Y. */
	static double rigYaw(double gameX, double gameZ) { return Math.toDegrees(Math.atan2(gameZ, gameX)); }
}

package com.voxon.capturerig;

import com.mojang.blaze3d.pipeline.RenderTarget;
import com.mojang.blaze3d.pipeline.TextureTarget;
import com.mojang.blaze3d.platform.InputConstants;
import com.mojang.blaze3d.systems.RenderSystem;
import com.mojang.renderpearl.api.GpuFormat;
import com.mojang.renderpearl.api.buffers.GpuBuffer;
import com.mojang.renderpearl.api.buffers.GpuBufferSlice;
import com.mojang.renderpearl.api.textures.GpuTexture;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import net.minecraft.client.Camera;
import net.minecraft.client.DeltaTracker;
import net.minecraft.client.Minecraft;
import net.minecraft.client.renderer.GameRenderer;
import net.minecraft.client.renderer.state.GameRenderState;
import net.minecraft.world.phys.Vec3;
import org.joml.Vector3fc;

/** The cube of cameras: each face is the game's own level render, drawn once more in the frame before the player's,
 * from a camera standing on a face of the volume and looking in, into a target of the face's own. The game draws it as
 * it draws its panorama screenshots - update, extract, render - with the camera's pose, lens and target the face's
 * (the mixins). Each face's colour and depth are read back through the game's renderer (OpenGL or Vulkan alike) into
 * the shared file (FaceFile), and the decode host beside the game (voxon_capture_mc.exe, started here) does the rest,
 * with the add-on's own decoder. */
public final class FaceRig {
	// The face being drawn, read by the mixins: its camera's place, turn, vertical lens and clip planes, and its target.
	public static boolean active;
	public static Vec3 pos = Vec3.ZERO;
	public static float yaw, pitch, fovV, near, reach;
	public static RenderTarget target;

	private static final RenderTarget[] TARGETS = new RenderTarget[6];
	private static final FaceFile FILE = new FaceFile();
	private static RigSettings cfg;
	private static Geometry geo;
	private static String rawSeen;
	private static long settingsAt, nextShot, statsAt, shots, shotNanos, hostTried;
	private static Process host;
	private static boolean saidNoIni, saidNoHost, on = true, f7Down, facesDrawn;

	private FaceRig() { }

	/** Before the player's own frame: the settings, now and then, and the faces when a capture is due. */
	public static void beforeFrame(Minecraft mc, DeltaTracker deltaTracker) {
		if (mc.level == null || mc.player == null) return;
		// F7 stops and starts the faces, as in the other rigs.
		boolean f7 = InputConstants.isKeyDown(InputConstants.KEY_F7);
		if (f7 && !f7Down) {
			on = !on;
			VoxonRig.LOG.info(on ? "started (F7)" : "stopped (F7 starts it again)");
		}
		f7Down = f7;
		long now = System.nanoTime();
		Path dir = mc.gameDirectory.toPath().toAbsolutePath().normalize();
		if (now - settingsAt > 500_000_000L) {
			settingsAt = now;
			reload(dir);
		}
		if (cfg == null || !cfg.on || !on) return;
		long period = 1_000_000_000L / cfg.hz;
		if (now < nextShot) return;
		nextShot = Math.max(nextShot + period, now + period / 2);
		keepHost(dir);
		capture(mc, deltaTracker);
		shots++;
		shotNanos += System.nanoTime() - now;
		if (now - statsAt > 5_000_000_000L) {
			if (statsAt != 0) VoxonRig.LOG.info("capturing: {} captures/s, {} ms each, the game at {} fps",
				String.format("%.1f", shots / ((now - statsAt) / 1e9)), String.format("%.1f", shotNanos / 1e6 / Math.max(1, shots)), mc.getFps());
			statsAt = now;
			shots = 0;
			shotNanos = 0;
		}
	}

	private static void reload(Path dir) {
		RigSettings c = RigSettings.load(dir.resolve("voxon_capture_ue.ini"));
		if (c == null) {
			if (!saidNoIni) VoxonRig.LOG.info("no voxon_capture_ue.ini in {}: Install rig puts it there", dir);
			saidNoIni = true;
			cfg = null;
			return;
		}
		saidNoIni = false;
		cfg = c;
		geo = Geometry.derive(c);
		if (!c.raw.equals(rawSeen)) {
			rawSeen = c.raw;
			StringBuilder faces = new StringBuilder();
			for (int v = 0; v < 6; v++) if (c.faceOn[v]) faces.append(v);
			VoxonRig.LOG.info("settings: faces {}, {} px, radius {} m, tilt {}, yaw {}{}, feet ({}, {}, {}), {} Hz{}", faces, c.faceRes,
				c.radiusM, c.tiltDeg, c.yawDeg, c.turnWithPlayer ? " turning with the player" : "", c.feetX, c.feetY, c.feetZ, c.hz,
				c.orthoAsked ? " - orthographic lenses are not drawn in Minecraft: perspective" : "");
		}
	}

	/** The decode host, started beside the game and again if it has ended; it ends with the game. */
	private static void keepHost(Path dir) {
		if (host != null && host.isAlive()) return;
		long now = System.nanoTime();
		if (now - hostTried < 5_000_000_000L) return;
		hostTried = now;
		Path exe = dir.resolve("voxon_capture_mc.exe");
		if (!Files.exists(exe)) {
			if (!saidNoHost) VoxonRig.LOG.warn("no {}: Install rig puts it there", exe);
			saidNoHost = true;
			return;
		}
		try {
			host = new ProcessBuilder(exe.toString(), dir.toString(), Long.toString(ProcessHandle.current().pid()))
				.redirectErrorStream(true).redirectOutput(ProcessBuilder.Redirect.DISCARD).start();
			VoxonRig.LOG.info("decode host started: {}", exe);
		} catch (IOException e) {
			VoxonRig.LOG.warn("decode host: {}", e.toString());
		}
	}

	private static void capture(Minecraft mc, DeltaTracker deltaTracker) {
		RigSettings c = cfg;
		Geometry g = geo;
		int res = c.faceRes, side = Geometry.sideHeight(c);
		try {
			if (!FILE.ready(res, side)) return;
		} catch (IOException e) {
			VoxonRig.LOG.warn("faces file: {}", e.toString());
			return;
		}
		GameRenderer gr = mc.gameRenderer;
		Camera camera = gr.mainCamera();

		// Turning with the player, the rig turns by the player's view as well as by its own yaw; the decode turns the
		// volume back by the yaw alone (capture_gpu.hpp, DisplayTurnDeg). The Unity rig's Capture, term for term.
		double rigYaw = c.yawDeg, dispTurn = c.yawDeg;
		if (c.turnWithPlayer) {
			Vector3fc f = camera.forwardVector();
			if (f.x() * f.x() + f.z() * f.z() > 1e-6) rigYaw += Geometry.rigYaw(f.x(), f.z());
			dispTurn = c.yawDeg - 90;
		}
		Vec3 feet = mc.player.getPosition(deltaTracker.getGameTimeDeltaPartialTick(true));
		Vec3 centre = g.centre(c, feet, rigYaw, dispTurn);

		int mask = 0;
		for (int v = 0; v < 6; v++) if (c.faceOn[v]) mask |= 1 << v;
		if (mask == 0) return;
		final int faceMask = mask;
		final int slot = FILE.begin();
		final int[] left = { Integer.bitCount(mask) * 2 };

		boolean smartCull = mc.smartCull;
		try {
			camera.enablePanoramicMode();
			mc.smartCull = false;                  // a camera outside the terrain: the occlusion graph would cull it all
			for (int v = 0; v < 6; v++) {
				if (!c.faceOn[v]) continue;
				int w = res, h = Geometry.faceHeight(v, c);
				if (TARGETS[v] == null || TARGETS[v].width != w || TARGETS[v].height != h) {
					if (TARGETS[v] != null) TARGETS[v].destroyBuffers();
					TARGETS[v] = new TextureTarget("VoxonRig face " + v, w, h, GpuFormat.RGBA8_UNORM, GpuFormat.D32_FLOAT);
				}
				Geometry.Pose p = g.place(v, c, centre, rigYaw);
				pos = p.pos();
				yaw = p.yaw();
				pitch = p.pitch();
				fovV = p.fovV();
				near = p.near();
				reach = p.far();
				target = TARGETS[v];
				active = true;
				gr.update(deltaTracker);
				gr.extract(deltaTracker, true);
				steady(gr.gameRenderState());
				gr.renderLevel();
				active = false;

				// The face's colour and depth, read back once the GPU has drawn them, into the slot; the slot handed
				// over when the last of them is in.
				final int face = v;
				readBack(TARGETS[v].getColorTexture(), (long)w * h * 4, data -> FILE.put(slot, face, false, data), () -> {
					if (--left[0] == 0) FILE.commit(slot, faceMask);
				});
				readBack(TARGETS[v].getDepthTexture(), (long)w * h * 4, data -> FILE.put(slot, face, true, data), () -> {
					if (--left[0] == 0) FILE.commit(slot, faceMask);
				});
				// Each face ends as a frame does (Minecraft.renderFrame): a submit of its own, and the per-frame
				// buffers turned over. The renderer's ring buffers will not take a second level render in one submit.
				RenderSystem.getDevice().createCommandEncoder().submit();
				RenderSystem.getDynamicUniforms().reset();
				mc.levelRenderer.endFrame();
			}
		} catch (Exception e) {
			VoxonRig.LOG.error("faces: ", e);
			cfg = null;                            // stopped until the settings are read again
		} finally {
			active = false;
			camera.disablePanoramicMode();
			mc.smartCull = smartCull;
			facesDrawn = true;
		}
	}

	/** True once after the faces: the player's frame then picks its own chunk sections again (LevelExtractorMixin). */
	public static boolean consumeFacesDrawn() {
		boolean was = facesDrawn;
		facesDrawn = false;
		return was;
	}

	/** The game's own shakes, out of a face: the view bob, the tilt when hurt or dying, and the spin of nausea or a
	 * portal, which renderLevel puts into the lens - each face would shake its own way, and the decode knows none of
	 * it. The face's render state only: the player's frame extracts its own afterwards. */
	private static void steady(GameRenderState s) {
		s.optionsRenderState.bobView = false;
		s.optionsRenderState.screenEffectScale = 0;
		s.levelRenderState.cameraRenderState.entityRenderState.isLiving = false;
	}

	private static void readBack(GpuTexture texture, long bytes, java.util.function.Consumer<java.nio.ByteBuffer> into, Runnable done) {
		GpuBuffer buffer = RenderSystem.getDevice().createBuffer(() -> "VoxonRig readback", 9, bytes);
		RenderSystem.getDevice().createCommandEncoder().copyTextureToBuffer(texture, buffer, 0L, () -> {
			try (GpuBufferSlice.MappedView read = buffer.map(true, false)) {
				into.accept(read.data());
			}
			buffer.close();
			done.run();
		}, 0);
	}
}

package com.voxon.capturerig;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

/** The rig's settings: voxon_capture_ue.ini in the game's folder, then the app's control file over it when that file
 * names this game. The same keys, defaults and limits as the Unity rig's RigSettings (rig/unity) and the add-on's
 * LoadSettings (addon/capture_gpu.hpp), because the decode host reads the same files and the two must agree exactly. */
final class RigSettings {
	boolean on = true;
	float radiusM = 12f, feetX = 0f, feetY = 0f, feetZ = 1.5f, boundR = 4f, boundZ = 2f;
	boolean[] faceOn = { true, true, false, false, true, false };
	int faceRes = 512;
	float tiltDeg = 30f;
	int hz = 30;
	float camDist = 1f, yawDeg = 0f;
	float scaleX = 1f, scaleY = 1f, scaleZ = 1f;
	boolean turnWithPlayer, perspective = true, orthoAsked;
	String controlFile = "", hostGame = "", engine = "";
	String raw = "";

	/** The settings now, or null without the ini. */
	static RigSettings load(Path ini) {
		if (!Files.exists(ini)) return null;
		RigSettings c = new RigSettings();
		String text;
		try { text = Files.readString(ini, StandardCharsets.UTF_8); }
		catch (IOException e) { return null; }
		apply(text, c, false);
		c.raw = text;
		if (!c.controlFile.isEmpty()) {
			try {
				String host = Files.readString(Path.of(c.controlFile), StandardCharsets.UTF_8);
				if (c.hostGame.equals(value(host, "game"))) {
					apply(host, c, true);
					c.raw += host;
				}
			} catch (IOException | RuntimeException ignored) { }      // the app is writing it: the next read takes it
		}
		c.faceRes = Math.max(64, Math.min(2560, c.faceRes)) / 2 * 2;
		c.camDist = Math.max(1f, Math.min(8f, c.camDist));
		c.scaleX = Math.max(0.05f, c.scaleX);
		c.scaleY = Math.max(0.05f, c.scaleY);
		c.scaleZ = Math.max(0.05f, c.scaleZ);
		if (c.boundR <= 0) c.boundR = 4f;
		if (c.boundZ <= 0) c.boundZ = 2f;
		c.radiusM = Math.max(0.5f, Math.min(1000f, c.radiusM));
		// Perspective lenses only: the game's projection has no centred orthographic one. The decode host holds to the same.
		c.orthoAsked = !c.perspective;
		c.perspective = true;
		return c;
	}

	private static void apply(String text, RigSettings c, boolean host) {
		for (String line : text.split("\n")) {
			int semi = line.indexOf(';');
			if (semi >= 0) line = line.substring(0, semi);
			int eq = line.indexOf('=');
			if (eq <= 0) continue;
			String k = line.substring(0, eq).trim(), v = line.substring(eq + 1).trim();
			if (host) {
				switch (k) {   // the app's Cameras tab
					case "ueFaces" -> setFaces(c, v);
					case "ueFaceRes" -> c.faceRes = (int)f(v, c.faceRes);
					case "ueRadius" -> c.radiusM = f(v, c.radiusM);
					case "ueCamDist" -> c.camDist = f(v, c.camDist);
					case "ueYaw" -> c.yawDeg = f(v, c.yawDeg);
					case "ueTilt" -> c.tiltDeg = f(v, c.tiltDeg);
					case "uePerspective" -> c.perspective = f(v, 1) != 0;
					case "ueFeetX" -> c.feetX = f(v, c.feetX);
					case "ueFeetY" -> c.feetY = f(v, c.feetY);
					case "ueFeetZ" -> c.feetZ = f(v, c.feetZ);
					case "ueTurn" -> c.turnWithPlayer = f(v, 0) != 0;
					case "ueScaleX" -> c.scaleX = f(v, c.scaleX);
					case "ueScaleY" -> c.scaleY = f(v, c.scaleY);
					case "ueScaleZ" -> c.scaleZ = f(v, c.scaleZ);
					case "ueBoundR" -> c.boundR = f(v, c.boundR);     // the app's Display
					case "ueBoundZ" -> c.boundZ = f(v, c.boundZ);
					default -> { }
				}
				continue;
			}
			switch (k) {
				case "on" -> c.on = f(v, 1) != 0;
				case "radiusM" -> c.radiusM = f(v, c.radiusM);
				case "feetX" -> c.feetX = f(v, c.feetX);
				case "feetY" -> c.feetY = f(v, c.feetY);
				case "feetZ" -> c.feetZ = f(v, c.feetZ);
				case "boundR" -> c.boundR = f(v, c.boundR);
				case "boundZ" -> c.boundZ = f(v, c.boundZ);
				case "faces" -> setFaces(c, v);
				case "faceRes" -> c.faceRes = (int)f(v, c.faceRes);
				case "tiltDeg" -> c.tiltDeg = f(v, c.tiltDeg);
				case "hz" -> c.hz = Math.max(1, (int)f(v, c.hz));
				case "camDist" -> c.camDist = f(v, c.camDist);
				case "yawDeg" -> c.yawDeg = f(v, c.yawDeg);
				case "turnWithPlayer" -> c.turnWithPlayer = f(v, 0) != 0;
				case "scaleX" -> c.scaleX = f(v, c.scaleX);
				case "scaleY" -> c.scaleY = f(v, c.scaleY);
				case "scaleZ" -> c.scaleZ = f(v, c.scaleZ);
				case "perspective" -> c.perspective = f(v, 1) != 0;
				case "controlFile" -> c.controlFile = v;
				case "hostGame" -> c.hostGame = v;
				case "engine" -> c.engine = v;
				default -> { }
			}
		}
	}

	private static void setFaces(RigSettings c, String v) {
		c.faceOn = new boolean[6];
		for (char ch : v.toCharArray()) if (ch >= '0' && ch <= '5') c.faceOn[ch - '0'] = true;
	}

	private static float f(String v, float fallback) {
		try { return Float.parseFloat(v); }
		catch (NumberFormatException e) { return fallback; }
	}

	private static String value(String text, String key) {
		for (String line : text.split("\n")) {
			int eq = line.indexOf('=');
			if (eq > 0 && line.substring(0, eq).trim().equals(key)) return line.substring(eq + 1).trim();
		}
		return null;
	}
}

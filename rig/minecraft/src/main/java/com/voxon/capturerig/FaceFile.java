package com.voxon.capturerig;

import java.io.IOException;
import java.lang.invoke.VarHandle;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.MappedByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;

/** THE SHARED FILE: each capture's pictures, handed to the decode host (addon/minecraft_host.cpp, which reads the same
 * layout: change both together). %TEMP%\voxon_capture_mc.faces: a 4096-byte header, then SLOTS slots, each a 256-byte
 * header and then every face's picture at a fixed place - its colour (RGBA8) and then its depth (float, reversed: 1
 * near, 0 far), rows bottom-up, as the game reads them back. A slot is filled, then its sequence set, then the header's newest slot and
 * sequence: the host takes only a slot whose sequence is the newest. */
final class FaceFile {
	static final int MAGIC = 0x434D5856, VERSION = 1, SLOTS = 3, HEADER = 4096, SLOT_HEADER = 256;
	static final Path PATH = Path.of(System.getProperty("java.io.tmpdir"), "voxon_capture_mc.faces");

	private FileChannel channel;
	private MappedByteBuffer map;
	private int faceRes, sideH, slot = -1;
	private long slotBytes, seq;

	static long faceBytes(int v, int faceRes, int sideH) { return (long)faceRes * (v >= 4 ? faceRes : sideH) * 8; }

	private long faceOffset(int v) {
		long o = SLOT_HEADER;
		for (int i = 0; i < v; i++) o += faceBytes(i, faceRes, sideH);
		return o;
	}

	/** The file for faces of this size: made again when the size changes. False when it cannot be. */
	boolean ready(int res, int side) throws IOException {
		if (map != null && res == faceRes && side == sideH) return true;
		close();
		faceRes = res;
		sideH = side;
		slotBytes = SLOT_HEADER;
		for (int v = 0; v < 6; v++) slotBytes += faceBytes(v, res, side);
		long total = HEADER + SLOTS * slotBytes;
		channel = FileChannel.open(PATH, StandardOpenOption.CREATE, StandardOpenOption.READ, StandardOpenOption.WRITE);
		map = channel.map(FileChannel.MapMode.READ_WRITE, 0, total);
		map.order(ByteOrder.LITTLE_ENDIAN);
		map.putInt(0, 0);                          // not ours until the header is whole
		map.putInt(4, VERSION);
		map.putInt(8, SLOTS);
		map.putInt(12, (int)slotBytes);
		map.putLong(16, 0);
		map.putInt(24, 0);
		map.putInt(28, res);
		map.putInt(32, side);
		VarHandle.releaseFence();
		map.putInt(0, MAGIC);
		return true;
	}

	/** The next slot to fill, marked as not yet whole. */
	int begin() {
		slot = (slot + 1) % SLOTS;
		map.putLong((int)slotBase(slot), 0);
		return slot;
	}

	/** One face's colour (depth: false) or depth (true), as read back, into a slot. */
	void put(int s, int v, boolean depth, ByteBuffer data) {
		long at = slotBase(s) + faceOffset(v) + (depth ? (long)faceRes * (v >= 4 ? faceRes : sideH) * 4 : 0);
		int n = Math.min(data.remaining(), (int)(faceBytes(v, faceRes, sideH) / 2));
		map.put((int)at, data, data.position(), n);
	}

	/** A slot whole: its faces, then its sequence, then the header's newest. */
	void commit(int s, int faceMask) {
		long base = slotBase(s);
		map.putInt((int)base + 8, faceMask);
		map.putInt((int)base + 12, faceRes);
		map.putInt((int)base + 16, sideH);
		seq++;
		VarHandle.releaseFence();
		map.putLong((int)base, seq);
		VarHandle.releaseFence();
		map.putInt(24, s);
		map.putLong(16, seq);
	}

	private long slotBase(int s) { return HEADER + s * slotBytes; }

	void close() {
		map = null;
		if (channel != null) {
			try { channel.close(); } catch (IOException ignored) { }
			channel = null;
		}
	}
}

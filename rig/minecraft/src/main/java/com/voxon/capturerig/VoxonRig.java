package com.voxon.capturerig;

import net.fabricmc.api.ClientModInitializer;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/** The rig's entry point: a cube of cameras round the player, drawn in the same frame as the player's view. */
public final class VoxonRig implements ClientModInitializer {
	public static final Logger LOG = LoggerFactory.getLogger("Voxon Capture Rig");

	@Override
	public void onInitializeClient() {
		LOG.info("armed");
	}
}

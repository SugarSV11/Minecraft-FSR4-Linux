package dev.fsr4linux.client;

import dev.fsr4linux.core.Notification;
import net.fabricmc.fabric.api.client.rendering.v1.hud.HudElement;
import net.minecraft.client.DeltaTracker;
import net.minecraft.client.Minecraft;
import net.minecraft.client.gui.Font;
import net.minecraft.client.gui.GuiGraphicsExtractor;

/** Discreet notification (about 2 s) in the top-left corner plus an optional debug overlay; never covers hotbar, health, chat or crosshair. */
public final class FsrHud {
	private FsrHud() { }

	public static void render(GuiGraphicsExtractor g, DeltaTracker dt) {
		Minecraft mc = Minecraft.getInstance();
		Font font = mc.font;
		long now = System.currentTimeMillis();
		var toasts = Fsr4LinuxClient.toasts();
		while (!toasts.isEmpty() && toasts.peekFirst().until() < now) toasts.pollFirst();
		int y = 6;
		for (var t : toasts) {
			Notification n = t.n();
			long left = t.until() - now;
			float a = Math.min(1f, left / 300f);   // fade out in the last 300 ms
			int alpha = (int) (a * 255) & 0xFF;
			int w = font.width(n.text()) + 12;
			int bg = ((int) (a * 170) << 24) | 0x101418;
			int accent = (alpha << 24) | (n.error() ? 0xE5484D : 0x3DD68C);
			g.fill(6, y, 6 + w, y + 16, bg);
			g.fill(6, y, 8, y + 16, accent);
			g.text(font, n.text(), 12, y + 4, (alpha << 24) | 0xF2F4F8, false);
			y += 19;
		}
		var cfg = Fsr4LinuxClient.controller() == null ? null : Fsr4LinuxClient.controller().config();
		if (cfg != null && cfg.debugOverlay) {
			FsrRuntime rt = FsrRuntime.get();
			String running = Fsr4LinuxClient.controller().runningLabel();
			String l1 = "FSR: " + (running == null ? (cfg.enabled ? "ACTIVANDO, ESPERANDO PRIMER CUADRO" : "DESACTIVADO") : "ACTIVO " + running);
			String l2 = rt.isAllocated() ? "interna " + rt.renderWidth() + "x" + rt.renderHeight() + " -> salida " + rt.outputWidth() + "x" + rt.outputHeight() : "sin reescalado";
			String l3 = String.format("GPU dispatch %.2f ms | helper %.2f ms | puente (hilo render) %.2f ms | frame %.1f ms", rt.lastGpuDispatchMs, rt.lastHelperMs, rt.lastBridgeFrameMs, Fsr4LinuxClient.lastFrameMs());
			int yy = y + 4;
			for (String s : new String[]{l1, l2, l3}) { g.fill(6, yy, 12 + font.width(s) + 6, yy + 12, 0x99101418); g.text(font, s, 9, yy + 2, 0xFFE8EAF0, false); yy += 13; }
		}
		if (cfg != null && cfg.fpsCounter) {
			String s = String.format("%.0f FPS", 1000.0 / Math.max(0.1f, Fsr4LinuxClient.lastFrameMs()));
			int x = g.guiWidth() - font.width(s) - 10;
			g.fill(x - 4, 3, x + font.width(s) + 4, 15, 0x99101418); g.text(font, s, x, 5, 0xFFE8EAF0, false);
		}
	}
}

#include "prospero_sdk.h"

namespace {

struct HardwareLab {
	ProsperoRuntime rt;
	Ps5PadData      pad {};
	uint32_t        prev_btns     = 0;
	int             lightbar_idx  = 0;
	int             pacing_x      = 120;
	int             pacing_dir    = 12;

	void StepAndRender() {
		rt.ReadPad(&pad);
		const uint32_t pressed = pad.buttons & ~prev_btns;
		prev_btns              = pad.buttons;

		static const Ps5LightBarColor LIGHTS[4] = {
		    {214, 178, 108, 0}, // Champagne Gold
		    {76, 208, 238, 0},  // Cyber Cyan
		    {78, 209, 146, 0},  // Emerald
		    {232, 88, 104, 0},  // Crimson
		};
		static const char* LIGHT_NAMES[4] = {"CHAMPAGNE GOLD", "CYBER CYAN", "EMERALD", "CRIMSON"};

		if ((pressed & PS5_BTN_TRIANGLE) != 0 && rt.pad_handle >= 0) {
			lightbar_idx = (lightbar_idx + 1) & 3;
			ps5_PadSetLightBar(rt.pad_handle, &LIGHTS[lightbar_idx]);
		}

		pacing_x += pacing_dir;
		if (pacing_x >= 1740 || pacing_x <= 120) {
			pacing_dir = -pacing_dir;
		}

		rt.Clear(COL_BG_DEEP, MakeArgb(24, 28, 35));

		// Header Card
		rt.FillRectV(80, 28, 1760, 96, COL_BG_PANEL);
		rt.DrawBorderV(80, 28, 1760, 96, 2, COL_GOLD);
		rt.DrawTextV(114, 48, 4, "RAZOR HARDWARE & RESOLUTION LAB", COL_GOLD_BRIGHT);
		rt.DrawTextV(114, 88, 2, "PROSPERO VIDEOOUT + DUALSENSE + 60 HZ FRAME-PACING SUITE", COL_MUTED);
		rt.DrawTextV(1380, 52, 3, rt.ResolutionLabel(), COL_EMERALD);
		rt.DrawTextV(1380, 88, 2, "PPSA90003 - RAZOR STORE", COL_MUTED);

		// Left Card: VideoOut & Resolution Pipeline Status
		rt.FillRectV(80, 150, 850, 410, COL_BG_PANEL);
		rt.DrawBorderV(80, 150, 850, 410, 2, COL_STEEL_BORDER);
		rt.DrawTextV(114, 178, 3, "ACTIVE PROSPERO VIDEOOUT SURFACE", COL_GOLD);

		rt.DrawTextV(114, 232, 3, "OUTPUT MODE:", COL_MUTED);
		rt.DrawTextV(420, 232, 3, rt.ResolutionLabel(), COL_SILVER);

		char buf[64];
		char num[16];
		buf[0] = '\0';
		FormatInt(num, static_cast<int>(rt.width));
		StrAppend(buf, num);
		StrAppend(buf, " X ");
		FormatInt(num, static_cast<int>(rt.height));
		StrAppend(buf, num);
		StrAppend(buf, "  (TILED 64KB BLOCKS)");
		rt.DrawTextV(114, 284, 3, "SURFACE:", COL_MUTED);
		rt.DrawTextV(420, 284, 2, buf, COL_CYAN);

		buf[0] = '\0';
		FormatInt(num, static_cast<int>(rt.frame_counter));
		StrAppend(buf, num);
		StrAppend(buf, " FRAMES PRESENTED (VSYNC)");
		rt.DrawTextV(114, 332, 3, "VBLANK:", COL_MUTED);
		rt.DrawTextV(420, 336, 2, buf, COL_EMERALD);

		rt.DrawTextV(114, 384, 3, "LIGHTBAR:", COL_MUTED);
		rt.DrawTextV(420, 388, 2, LIGHT_NAMES[lightbar_idx],
		             MakeArgb(LIGHTS[lightbar_idx].r, LIGHTS[lightbar_idx].g, LIGHTS[lightbar_idx].b));
		rt.DrawTextV(114, 434, 2, "TIP: CHANGE PC RESOLUTION IN LAUNCHER HEADER TO TEST", COL_MUTED);
		rt.DrawTextV(114, 464, 2, "1080P FHD / 1440P WQHD / 4K UHD SCALING!", COL_GOLD_BRIGHT);
		rt.DrawTextV(114, 504, 2, "PRESS TRIANGLE TO CYCLE DUALSENSE LIGHTBAR COLOR", COL_SILVER);

		// Right Card: Live DualSense / Keyboard Controller Telemetry
		rt.FillRectV(970, 150, 870, 410, COL_BG_PANEL);
		rt.DrawBorderV(970, 150, 870, 410, 2, COL_STEEL_BORDER);
		rt.DrawTextV(1004, 178, 3, "DUALSENSE / HOST INPUT TELEMETRY", COL_GOLD);

		// Left & Right Analog Stick Scopes
		const int ls_cx = 1140;
		const int ls_cy = 340;
		const int rs_cx = 1400;
		const int rs_cy = 340;
		rt.FillRectV(ls_cx - 90, ls_cy - 90, 180, 180, COL_BG_DEEP);
		rt.DrawBorderV(ls_cx - 90, ls_cy - 90, 180, 180, 2, COL_STEEL_BORDER);
		rt.FillCircleV(ls_cx + ((static_cast<int>(pad.left_stick_x) - 128) * 75) / 128,
		               ls_cy + ((static_cast<int>(pad.left_stick_y) - 128) * 75) / 128, 12, COL_GOLD_BRIGHT);
		rt.DrawCenteredTextV(ls_cx, ls_cy + 104, 2, "LEFT STICK", COL_MUTED);

		rt.FillRectV(rs_cx - 90, rs_cy - 90, 180, 180, COL_BG_DEEP);
		rt.DrawBorderV(rs_cx - 90, rs_cy - 90, 180, 180, 2, COL_STEEL_BORDER);
		rt.FillCircleV(rs_cx + ((static_cast<int>(pad.right_stick_x) - 128) * 75) / 128,
		               rs_cy + ((static_cast<int>(pad.right_stick_y) - 128) * 75) / 128, 12, COL_CYAN);
		rt.DrawCenteredTextV(rs_cx, rs_cy + 104, 2, "RIGHT STICK", COL_MUTED);

		// L2 / R2 Analog Bars + Face / D-Pad indicators
		rt.DrawTextV(1540, 250, 2, "L2 TRIGGER", COL_MUTED);
		rt.FillRectV(1540, 274, 250, 20, COL_BG_DEEP);
		rt.FillRectV(1540, 274, (250 * pad.analog_l2) / 255, 20, COL_CYAN);
		rt.DrawBorderV(1540, 274, 250, 20, 2, COL_STEEL_BORDER);

		rt.DrawTextV(1540, 314, 2, "R2 TRIGGER", COL_MUTED);
		rt.FillRectV(1540, 338, 250, 20, COL_BG_DEEP);
		rt.FillRectV(1540, 338, (250 * pad.analog_r2) / 255, 20, COL_GOLD_BRIGHT);
		rt.DrawBorderV(1540, 338, 250, 20, 2, COL_STEEL_BORDER);

		const struct BtnVis {
			uint32_t    mask;
			const char* name;
			int         x;
			int         y;
		} btns[8] = {
		    {PS5_BTN_CROSS, "CROSS", 1540, 390},    {PS5_BTN_CIRCLE, "CIRCLE", 1675, 390},
		    {PS5_BTN_SQUARE, "SQUARE", 1540, 430},  {PS5_BTN_TRIANGLE, "TRIANG", 1675, 430},
		    {PS5_BTN_UP, "DPAD-U", 1540, 470},      {PS5_BTN_DOWN, "DPAD-D", 1675, 470},
		    {PS5_BTN_LEFT, "DPAD-L", 1540, 510},    {PS5_BTN_RIGHT, "DPAD-R", 1675, 510},
		};
		for (int i = 0; i < 8; ++i) {
			const bool on = (pad.buttons & btns[i].mask) != 0;
			rt.FillRectV(btns[i].x, btns[i].y, 120, 30, on ? COL_GOLD : COL_BG_DEEP);
			rt.DrawBorderV(btns[i].x, btns[i].y, 120, 30, 2, on ? COL_GOLD_BRIGHT : COL_STEEL_BORDER);
			rt.DrawCenteredTextV(btns[i].x + 60, btns[i].y + 8, 2, btns[i].name,
			                     on ? COL_BG_DEEP : COL_SILVER);
		}

		// Bottom Card: Color Calibration & 60 FPS Motion Pacing Bar
		rt.FillRectV(80, 590, 1760, 440, COL_BG_PANEL);
		rt.DrawBorderV(80, 590, 1760, 440, 2, COL_STEEL_BORDER);
		rt.DrawTextV(114, 618, 3, "SRGB COLOR RAMP & 60 FPS MOTION PACING", COL_GOLD);

		const uint32_t bars[8] = {COL_SILVER, COL_GOLD_BRIGHT, COL_CYAN,    COL_EMERALD,
		                          COL_VIOLET, COL_CRIMSON,     COL_GOLD,    COL_BG_ELEVATED};
		for (int i = 0; i < 8; ++i) {
			rt.FillRectV(114 + i * 212, 668, 200, 140, bars[i]);
			rt.DrawBorderV(114 + i * 212, 668, 200, 140, 2, COL_STEEL_BORDER);
		}

		// Grayscale precision ramp
		for (int g = 0; g < 16; ++g) {
			const uint8_t v = static_cast<uint8_t>(g * 17);
			rt.FillRectV(114 + g * 106, 830, 102, 44, MakeArgb(v, v, v));
		}

		// Smooth 60 Hz Motion Bar
		rt.FillRectV(114, 900, 1692, 92, COL_BG_DEEP);
		rt.DrawBorderV(114, 900, 1692, 92, 2, COL_STEEL_BORDER);
		rt.FillRectV(pacing_x, 912, 56, 68, COL_GOLD_BRIGHT);
		rt.FillRectV(pacing_x + 22, 912, 12, 68, COL_SILVER);

		rt.Present();
	}
};

HardwareLab g_lab;

} // namespace

extern "C" __attribute__((visibility("default"), used)) void _start(void* /*params*/,
                                                                    void (* /*atexit_fn*/)()) {
	memset(&g_lab, 0, sizeof(g_lab));
	g_lab.pacing_x   = 120;
	g_lab.pacing_dir = 12;
	if (!g_lab.rt.Init()) {
		for (;;) {
			ps5_Usleep(100000);
		}
	}
	for (;;) {
		g_lab.StepAndRender();
	}
}

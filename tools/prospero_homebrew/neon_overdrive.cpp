#include "prospero_sdk.h"

namespace {

constexpr int MAX_CARS = 8;

struct TrafficCar {
	bool     active;
	int      lane;    // 0..3
	float    z;       // 0.0 (near player) .. 1000.0 (horizon)
	float    speed;
	bool     is_gold; // bonus energy pickup
	uint32_t color;
};

struct OverdriveGame {
	ProsperoRuntime rt;
	TrafficCar      cars[MAX_CARS];

	float    player_x     = 0.0f; // -1.5 .. +1.5 lanes
	float    speed_kmh    = 210.0f;
	float    road_scroll  = 0.0f;
	int      score        = 0;
	int      best_score   = 0;
	int      shield       = 100;
	int      hit_flash    = 0;
	bool     crashed      = false;
	uint32_t rng          = 0x9E3779B9u;
	uint32_t prev_btns    = 0;

	uint32_t NextRand() {
		rng ^= rng << 13u;
		rng ^= rng >> 17u;
		rng ^= rng << 5u;
		return rng;
	}

	void Reset() {
		player_x  = 0.0f;
		speed_kmh = 210.0f;
		score     = 0;
		shield    = 100;
		hit_flash = 0;
		crashed   = false;
		for (int i = 0; i < MAX_CARS; ++i) {
			cars[i].active  = true;
			cars[i].lane    = static_cast<int>(NextRand() % 4u);
			cars[i].z       = 250.0f + i * 115.0f;
			cars[i].speed   = 95.0f + static_cast<float>(NextRand() % 65u);
			cars[i].is_gold = (i == 3);
			cars[i].color   = cars[i].is_gold ? COL_GOLD_BRIGHT
			                                  : ((i % 2 == 0) ? COL_CRIMSON : COL_VIOLET);
		}
	}

	void RespawnCar(int i) {
		cars[i].active  = true;
		cars[i].lane    = static_cast<int>(NextRand() % 4u);
		cars[i].z       = 980.0f + static_cast<float>(NextRand() % 180u);
		cars[i].speed   = 95.0f + static_cast<float>(NextRand() % 80u);
		cars[i].is_gold = (NextRand() % 5u) == 0u;
		cars[i].color   = cars[i].is_gold ? COL_GOLD_BRIGHT
		                                  : ((NextRand() & 1u) != 0 ? COL_CRIMSON : COL_CYAN);
	}

	void Step() {
		Ps5PadData pad {};
		rt.ReadPad(&pad);
		const uint32_t pressed = pad.buttons & ~prev_btns;
		prev_btns              = pad.buttons;

		if (crashed) {
			if ((pressed & (PS5_BTN_CROSS | PS5_BTN_R2 | PS5_BTN_OPTIONS)) != 0 ||
			    pad.analog_r2 > 140) {
				Reset();
			}
			return;
		}

		float steer = 0.0f;
		if ((pad.buttons & PS5_BTN_LEFT) != 0) steer -= 1.0f;
		if ((pad.buttons & PS5_BTN_RIGHT) != 0) steer += 1.0f;
		const int sx = static_cast<int>(pad.left_stick_x) - 128;
		if (sx < -16 || sx > 16) {
			steer += static_cast<float>(sx) / 112.0f;
		}
		player_x += steer * 0.055f;
		if (player_x < -1.65f) player_x = -1.65f;
		if (player_x > 1.65f) player_x = 1.65f;

		const bool nitro = ((pad.buttons & (PS5_BTN_CROSS | PS5_BTN_R2 | PS5_BTN_R1)) != 0) ||
		                   pad.analog_r2 > 80;
		const bool brake = ((pad.buttons & (PS5_BTN_SQUARE | PS5_BTN_L2)) != 0) ||
		                   pad.analog_l2 > 80;

		const float target_speed = nitro ? 360.0f : (brake ? 145.0f : 235.0f);
		speed_kmh += (target_speed - speed_kmh) * 0.06f;

		road_scroll += speed_kmh * 0.14f;
		if (road_scroll >= 120.0f) road_scroll -= 120.0f;

		score += static_cast<int>(speed_kmh * 0.05f);
		if (score > best_score) best_score = score;
		if (hit_flash > 0) --hit_flash;

		for (int i = 0; i < MAX_CARS; ++i) {
			if (!cars[i].active) {
				RespawnCar(i);
				continue;
			}
			cars[i].z -= (speed_kmh - cars[i].speed) * 0.095f;
			if (cars[i].z < -40.0f) {
				score += 150;
				RespawnCar(i);
				continue;
			}
			if (cars[i].z > 1250.0f) {
				cars[i].z = 1000.0f;
			}

			// Player car sits around z in [40, 115]
			const float lane_x = (static_cast<float>(cars[i].lane) - 1.5f);
			float       diff_x = player_x - lane_x;
			if (diff_x < 0.0f) diff_x = -diff_x;
			if (cars[i].z >= 35.0f && cars[i].z <= 112.0f && diff_x < 0.42f) {
				if (cars[i].is_gold) {
					score += 1000;
					shield += 20;
					if (shield > 100) shield = 100;
					RespawnCar(i);
				} else {
					shield -= 34;
					hit_flash = 14;
					RespawnCar(i);
					if (shield <= 0) {
						shield  = 0;
						crashed = true;
					}
				}
			}
		}
	}

	void Render() {
		// Synthwave Twilight Sky + Horizon Sun
		rt.Clear(MakeArgb(14, 12, 28), MakeArgb(48, 24, 58));
		rt.FillCircleV(960, 430, 155, MakeArgb(235, 146, 78));
		rt.FillCircleV(960, 430, 125, COL_GOLD_BRIGHT);
		for (int s = 0; s < 5; ++s) {
			rt.FillRectV(790, 445 + s * 22, 340, 8, MakeArgb(38, 20, 48));
		}

		// Ground Plane (y = 470..1080)
		rt.FillRectV(0, 470, 1920, 610, MakeArgb(18, 21, 28));

		// Perspective 4-lane Highway Trapezoid Scanlines
		for (int y = 470; y < 1080; y += 6) {
			const float t          = static_cast<float>(y - 470) / 610.0f; // 0 at horizon -> 1 at bottom
			const int   half_road  = 90 + static_cast<int>(t * 680.0f);
			const int   center_off = static_cast<int>(-player_x * t * 110.0f);
			const int   cx         = 960 + center_off;

			const int stripe_phase = (static_cast<int>(road_scroll + (1.0f - t) * 900.0f) / 45) & 1;
			const uint32_t curb_col = stripe_phase != 0 ? COL_GOLD : COL_STEEL_BORDER;
			const uint32_t road_col = stripe_phase != 0 ? MakeArgb(29, 33, 42) : MakeArgb(24, 28, 36);

			rt.FillRectV(cx - half_road - 22, y, 22, 6, curb_col);
			rt.FillRectV(cx + half_road, y, 22, 6, curb_col);
			rt.FillRectV(cx - half_road, y, half_road * 2, 6, road_col);

			if (stripe_phase != 0) {
				for (int l = 1; l <= 3; ++l) {
					const int lx = cx - half_road + (half_road * 2 * l) / 4;
					rt.FillRectV(lx - 4, y, 8, 6, COL_SILVER);
				}
			}
		}

		// Draw Traffic Cars (painter's order: farthest z first)
		for (int pass = 0; pass < MAX_CARS; ++pass) {
			int   best = -1;
			float maxz = -9999.0f;
			for (int i = 0; i < MAX_CARS; ++i) {
				if (cars[i].active && cars[i].z > maxz) {
					// mark drawn via sign flip trick avoided; simple selection
				}
			}
			(void)best;
		}
		// Simple 8-element insertion order by z descending:
		int order[MAX_CARS] = {0, 1, 2, 3, 4, 5, 6, 7};
		for (int i = 1; i < MAX_CARS; ++i) {
			const int key = order[i];
			int       j   = i - 1;
			while (j >= 0 && cars[order[j]].z < cars[key].z) {
				order[j + 1] = order[j];
				--j;
			}
			order[j + 1] = key;
		}

		for (int idx = 0; idx < MAX_CARS; ++idx) {
			const TrafficCar& c = cars[order[idx]];
			if (!c.active || c.z < 0.0f || c.z > 1000.0f) continue;
			const float t         = 1.0f - (c.z / 1000.0f);
			const int   sy        = 474 + static_cast<int>(t * t * 545.0f);
			const float half_road = 90.0f + (t * t) * 680.0f;
			const float center_x  = 960.0f - player_x * (t * t) * 110.0f;
			const float lane_norm = (static_cast<float>(c.lane) - 1.5f) / 2.0f; // -0.75 .. +0.75
			const int   sx        = static_cast<int>(center_x + lane_norm * half_road * 1.85f);
			const int   cw        = 24 + static_cast<int>(t * 130.0f);
			const int   ch        = 16 + static_cast<int>(t * 82.0f);

			if (c.is_gold) {
				rt.FillCircleV(sx, sy - ch / 2, cw / 3 + 4, COL_GOLD_BRIGHT);
				rt.FillCircleV(sx, sy - ch / 2, cw / 5 + 2, COL_SILVER);
			} else {
				rt.FillRectV(sx - cw / 2, sy - ch, cw, ch, c.color);
				rt.DrawBorderV(sx - cw / 2, sy - ch, cw, ch, 2, COL_SILVER);
				rt.FillRectV(sx - cw / 2 + 6, sy - ch / 3, cw / 4, 6, COL_GOLD_BRIGHT);
				rt.FillRectV(sx + cw / 2 - 6 - cw / 4, sy - ch / 3, cw / 4, 6, COL_GOLD_BRIGHT);
			}
		}

		// Player Hypercar
		const int px = 960 + static_cast<int>(player_x * 235.0f);
		const int py = 955;
		const uint32_t body_col = (hit_flash > 0) ? COL_CRIMSON : COL_BG_ELEVATED;
		rt.FillRectV(px - 92, py - 56, 184, 58, body_col);
		rt.FillRectV(px - 66, py - 84, 132, 32, COL_CYAN);
		rt.DrawBorderV(px - 92, py - 56, 184, 58, 3, COL_GOLD);
		// Taillights + Nitrous Exhaust
		rt.FillRectV(px - 82, py - 38, 36, 12, COL_CRIMSON);
		rt.FillRectV(px + 46, py - 38, 36, 12, COL_CRIMSON);
		if (speed_kmh > 280.0f) {
			rt.FillRectV(px - 26, py + 2, 20, 24, COL_CYAN);
			rt.FillRectV(px + 6, py + 2, 20, 24, COL_CYAN);
		}

		// Top HUD
		rt.FillRectV(70, 24, 1780, 84, COL_BG_PANEL);
		rt.DrawBorderV(70, 24, 1780, 84, 2, COL_GOLD);
		rt.DrawTextV(102, 42, 4, "RAZOR NEON OVERDRIVE", COL_GOLD_BRIGHT);
		rt.DrawTextV(102, 76, 2, "STEER: STICK/DPAD   NITRO: CROSS/R2   COLLECT GOLD CORES", COL_MUTED);

		char buf[64];
		char num[16];
		buf[0] = '\0';
		FormatInt(num, static_cast<int>(speed_kmh));
		StrAppend(buf, num);
		StrAppend(buf, " KM/H   SCORE: ");
		FormatInt(num, score);
		StrAppend(buf, num);
		rt.DrawTextV(770, 42, 4, buf, COL_SILVER);

		// Shield bar
		rt.DrawTextV(770, 78, 2, "SHIELD:", COL_CYAN);
		rt.FillRectV(880, 76, 240, 16, COL_BG_DEEP);
		rt.FillRectV(880, 76, (240 * shield) / 100, 16, shield > 35 ? COL_EMERALD : COL_CRIMSON);
		rt.DrawBorderV(880, 76, 240, 16, 2, COL_STEEL_BORDER);

		rt.DrawTextV(1385, 46, 3, rt.ResolutionLabel(), COL_EMERALD);
		rt.DrawTextV(1385, 76, 2, "PPSA90002 - RAZOR STORE", COL_MUTED);

		if (crashed) {
			rt.FillRectV(520, 520, 880, 150, COL_BG_PANEL);
			rt.DrawBorderV(520, 520, 880, 150, 3, COL_CRIMSON);
			rt.DrawCenteredTextV(960, 550, 4, "SHIELD CRITICAL - WRECKED!", COL_CRIMSON);
			rt.DrawCenteredTextV(960, 612, 3, "PRESS CROSS / R2 TO RESTART RACE", COL_GOLD_BRIGHT);
		}

		rt.Present();
	}
};

OverdriveGame g_race;

} // namespace

extern "C" __attribute__((visibility("default"), used)) void _start(void* /*params*/,
                                                                    void (* /*atexit_fn*/)()) {
	memset(&g_race, 0, sizeof(g_race));
	g_race.rng = 0x9E3779B9u;
	if (!g_race.rt.Init()) {
		for (;;) {
			ps5_Usleep(100000);
		}
	}
	g_race.Reset();
	for (;;) {
		g_race.Step();
		g_race.Render();
	}
}

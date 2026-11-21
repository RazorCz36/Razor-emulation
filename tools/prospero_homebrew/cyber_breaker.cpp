#include "prospero_sdk.h"

namespace {

constexpr int BRICK_COLS = 10;
constexpr int BRICK_ROWS = 6;
constexpr int MAX_SPARKS = 64;

struct Spark {
	float    x;
	float    y;
	float    vx;
	float    vy;
	int      life;
	uint32_t color;
};

struct GameState {
	ProsperoRuntime rt;
	int             brick_hp[BRICK_ROWS][BRICK_COLS];
	Spark           sparks[MAX_SPARKS];

	float paddle_x  = 960.0f;
	float paddle_w  = 210.0f;
	float ball_x    = 960.0f;
	float ball_y    = 900.0f;
	float ball_vx   = 8.5f;
	float ball_vy   = -9.5f;
	float trail_x[6] = {};
	float trail_y[6] = {};

	bool     launched   = false;
	bool     game_over  = false;
	int      score      = 0;
	int      high_score = 0;
	int      lives      = 3;
	int      stage      = 1;
	int      combo      = 0;
	uint32_t rng        = 0xC0FFEE42u;
	uint32_t prev_btns  = 0;

	uint32_t NextRand() {
		rng ^= rng << 13u;
		rng ^= rng >> 17u;
		rng ^= rng << 5u;
		return rng;
	}

	void SpawnSparks(float x, float y, uint32_t col) {
		int spawned = 0;
		for (int i = 0; i < MAX_SPARKS && spawned < 10; ++i) {
			if (sparks[i].life <= 0) {
				const int rx     = static_cast<int>(NextRand() % 200u) - 100;
				const int ry     = static_cast<int>(NextRand() % 200u) - 120;
				sparks[i].x      = x;
				sparks[i].y      = y;
				sparks[i].vx     = static_cast<float>(rx) * 0.06f;
				sparks[i].vy     = static_cast<float>(ry) * 0.06f;
				sparks[i].life   = 22 + static_cast<int>(NextRand() % 14u);
				sparks[i].color  = col;
				++spawned;
			}
		}
	}

	void ResetStage(int new_stage) {
		stage = new_stage;
		for (int r = 0; r < BRICK_ROWS; ++r) {
			for (int c = 0; c < BRICK_COLS; ++c) {
				if (r == 0) {
					brick_hp[r][c] = 3;
				} else if (r < 3) {
					brick_hp[r][c] = 2;
				} else {
					brick_hp[r][c] = 1;
				}
			}
		}
		ResetBall();
	}

	void ResetBall() {
		launched = false;
		combo    = 0;
		ball_x   = paddle_x;
		ball_y   = 918.0f;
		const float speed_scale = 1.0f + static_cast<float>(stage - 1) * 0.12f;
		ball_vx                 = 8.2f * speed_scale;
		ball_vy                 = -9.6f * speed_scale;
		for (int i = 0; i < 6; ++i) {
			trail_x[i] = ball_x;
			trail_y[i] = ball_y;
		}
	}

	uint32_t BrickColor(int row, int hp) const {
		if (hp >= 3) return COL_GOLD_BRIGHT;
		if (hp == 2) return (row % 2 == 0) ? COL_CYAN : COL_VIOLET;
		const uint32_t row_cols[6] = {COL_GOLD, COL_CYAN, COL_EMERALD, COL_GOLD, COL_CRIMSON, COL_CYAN};
		return row_cols[row % 6];
	}

	void Step() {
		Ps5PadData pad {};
		rt.ReadPad(&pad);
		const uint32_t pressed = pad.buttons & ~prev_btns;
		prev_btns              = pad.buttons;

		float move = 0.0f;
		if ((pad.buttons & PS5_BTN_LEFT) != 0) move -= 1.0f;
		if ((pad.buttons & PS5_BTN_RIGHT) != 0) move += 1.0f;
		const int sx = static_cast<int>(pad.left_stick_x) - 128;
		if (sx < -16 || sx > 16) {
			move += static_cast<float>(sx) / 112.0f;
		}
		if (move < -1.25f) move = -1.25f;
		if (move > 1.25f) move = 1.25f;

		const float boost = ((pad.buttons & (PS5_BTN_R1 | PS5_BTN_L1 | PS5_BTN_SQUARE)) != 0) ? 1.45f : 1.0f;
		paddle_x += move * 16.5f * boost;

		const float min_x = 120.0f + paddle_w * 0.5f;
		const float max_x = 1800.0f - paddle_w * 0.5f;
		if (paddle_x < min_x) paddle_x = min_x;
		if (paddle_x > max_x) paddle_x = max_x;

		const bool action = (pressed & (PS5_BTN_CROSS | PS5_BTN_R2 | PS5_BTN_OPTIONS | PS5_BTN_CIRCLE)) != 0 ||
		                    pad.analog_r2 > 140;

		if (game_over) {
			if (action) {
				score     = 0;
				lives     = 3;
				game_over = false;
				ResetStage(1);
			}
			return;
		}

		if (!launched) {
			ball_x = paddle_x;
			ball_y = 918.0f;
			for (int i = 0; i < 6; ++i) {
				trail_x[i] = ball_x;
				trail_y[i] = ball_y;
			}
			if (action) {
				launched = true;
			}
		} else {
			for (int i = 5; i > 0; --i) {
				trail_x[i] = trail_x[i - 1];
				trail_y[i] = trail_y[i - 1];
			}
			trail_x[0] = ball_x;
			trail_y[0] = ball_y;

			ball_x += ball_vx;
			ball_y += ball_vy;

			// Arena walls (x in [120, 1800], y top at 136)
			if (ball_x < 134.0f) {
				ball_x  = 134.0f;
				ball_vx = -ball_vx;
			} else if (ball_x > 1786.0f) {
				ball_x  = 1786.0f;
				ball_vx = -ball_vx;
			}
			if (ball_y < 150.0f) {
				ball_y  = 150.0f;
				ball_vy = -ball_vy;
			}

			// Paddle bounce
			if (ball_vy > 0.0f && ball_y >= 926.0f && ball_y <= 964.0f &&
			    ball_x >= paddle_x - paddle_w * 0.54f && ball_x <= paddle_x + paddle_w * 0.54f) {
				const float rel = (ball_x - paddle_x) / (paddle_w * 0.5f);
				ball_vx         = rel * 12.5f;
				ball_vy         = -((ball_vy < 8.5f) ? 9.5f : ball_vy);
				ball_y          = 924.0f;
				combo           = 0;
				SpawnSparks(ball_x, 934.0f, COL_GOLD_BRIGHT);
			}

			// Brick collisions
			int remaining = 0;
			for (int r = 0; r < BRICK_ROWS; ++r) {
				for (int c = 0; c < BRICK_COLS; ++c) {
					if (brick_hp[r][c] <= 0) continue;
					++remaining;
					const float bx = 165.0f + c * 160.0f;
					const float by = 185.0f + r * 52.0f;
					const float bw = 148.0f;
					const float bh = 38.0f;
					if (ball_x + 12.0f >= bx && ball_x - 12.0f <= bx + bw &&
					    ball_y + 12.0f >= by && ball_y - 12.0f <= by + bh) {
						const uint32_t col = BrickColor(r, brick_hp[r][c]);
						--brick_hp[r][c];
						++combo;
						score += (100 + combo * 25) * stage;
						if (score > high_score) high_score = score;
						SpawnSparks(ball_x, ball_y, col);
						ball_vy = -ball_vy;
						r       = BRICK_ROWS;
						break;
					}
				}
			}

			if (remaining == 0) {
				ResetStage(stage + 1);
			} else if (ball_y > 1035.0f) {
				--lives;
				if (lives <= 0) {
					game_over = true;
				} else {
					ResetBall();
				}
			}
		}

		for (int i = 0; i < MAX_SPARKS; ++i) {
			if (sparks[i].life > 0) {
				sparks[i].x += sparks[i].vx;
				sparks[i].y += sparks[i].vy;
				sparks[i].vy += 0.22f;
				--sparks[i].life;
			}
		}
	}

	void Render() {
		rt.Clear(MakeArgb(15, 18, 24), MakeArgb(26, 30, 38));

		// Top Luxury Command Header
		rt.FillRectV(80, 26, 1760, 82, COL_BG_PANEL);
		rt.DrawBorderV(80, 26, 1760, 82, 2, COL_GOLD);
		rt.DrawTextV(112, 44, 4, "RAZOR CYBER BREAKER", COL_GOLD_BRIGHT);
		rt.DrawTextV(112, 78, 2, "NATIVE PS5 PROSPERO ENGINE  -  60 FPS VSYNC", COL_MUTED);

		char buf[64];
		buf[0] = '\0';
		StrAppend(buf, "SCORE: ");
		char num[16];
		FormatInt(num, score);
		StrAppend(buf, num);
		rt.DrawTextV(760, 44, 4, buf, COL_SILVER);

		buf[0] = '\0';
		StrAppend(buf, "STAGE ");
		FormatInt(num, stage);
		StrAppend(buf, num);
		StrAppend(buf, "   LIVES: ");
		FormatInt(num, lives);
		StrAppend(buf, num);
		rt.DrawTextV(760, 78, 2, buf, COL_CYAN);

		rt.DrawTextV(1380, 48, 3, rt.ResolutionLabel(), COL_EMERALD);
		rt.DrawTextV(1380, 78, 2, "PPSA90001 - RAZOR STORE", COL_MUTED);

		// Playfield Frame
		rt.FillRectV(114, 130, 1692, 906, MakeArgb(12, 14, 18));
		rt.DrawBorderV(114, 130, 1692, 906, 4, COL_STEEL_BORDER);

		// Bricks
		for (int r = 0; r < BRICK_ROWS; ++r) {
			for (int c = 0; c < BRICK_COLS; ++c) {
				const int hp = brick_hp[r][c];
				if (hp <= 0) continue;
				const int      bx  = 165 + c * 160;
				const int      by  = 185 + r * 52;
				const uint32_t col = BrickColor(r, hp);
				rt.FillRectV(bx, by, 148, 38, col);
				rt.DrawBorderV(bx, by, 148, 38, 2, COL_SILVER);
				if (hp >= 2) {
					rt.FillRectV(bx + 8, by + 6, 132, 5, COL_BG_DEEP);
				}
			}
		}

		// Sparks
		for (int i = 0; i < MAX_SPARKS; ++i) {
			if (sparks[i].life > 0) {
				rt.FillRectV(static_cast<int>(sparks[i].x) - 4, static_cast<int>(sparks[i].y) - 4,
				             8, 8, sparks[i].color);
			}
		}

		// Paddle
		const int px = static_cast<int>(paddle_x - paddle_w * 0.5f);
		const int pw = static_cast<int>(paddle_w);
		rt.FillRectV(px, 940, pw, 22, COL_GOLD);
		rt.FillRectV(px + 18, 944, pw - 36, 6, COL_GOLD_BRIGHT);
		rt.DrawBorderV(px, 940, pw, 22, 2, COL_SILVER);

		// Ball Trail & Plasma Core
		for (int i = 5; i >= 1; --i) {
			rt.FillCircleV(static_cast<int>(trail_x[i]), static_cast<int>(trail_y[i]), 11 - i,
			               MakeArgb(50, 110, 150));
		}
		rt.FillCircleV(static_cast<int>(ball_x), static_cast<int>(ball_y), 13, COL_CYAN);
		rt.FillCircleV(static_cast<int>(ball_x), static_cast<int>(ball_y), 7, COL_SILVER);

		if (!launched && !game_over) {
			rt.FillRectV(560, 580, 800, 96, COL_BG_PANEL);
			rt.DrawBorderV(560, 580, 800, 96, 2, COL_GOLD);
			rt.DrawCenteredTextV(960, 602, 3, "PRESS CROSS / R2 / ENTER TO LAUNCH", COL_GOLD_BRIGHT);
			rt.DrawCenteredTextV(960, 640, 2, "MOVE: LEFT STICK OR D-PAD / A-D   BOOST: L1 / R1", COL_SILVER);
		} else if (game_over) {
			rt.FillRectV(540, 540, 840, 140, COL_BG_PANEL);
			rt.DrawBorderV(540, 540, 840, 140, 3, COL_CRIMSON);
			rt.DrawCenteredTextV(960, 566, 4, "CORE DEPLETED - GAME OVER", COL_CRIMSON);
			rt.DrawCenteredTextV(960, 626, 3, "PRESS CROSS / R2 TO PLAY AGAIN", COL_GOLD_BRIGHT);
		}

		rt.Present();
	}
};

GameState g_game;

} // namespace

extern "C" __attribute__((visibility("default"), used)) void _start(void* /*params*/,
                                                                    void (* /*atexit_fn*/)()) {
	memset(&g_game, 0, sizeof(g_game));
	g_game.paddle_x = 960.0f;
	g_game.paddle_w = 210.0f;
	g_game.lives    = 3;
	g_game.stage    = 1;
	g_game.rng      = 0xC0FFEE42u;

	if (!g_game.rt.Init()) {
		for (;;) {
			ps5_Usleep(100000);
		}
	}

	g_game.ResetStage(1);
	for (;;) {
		g_game.Step();
		g_game.Render();
	}
}

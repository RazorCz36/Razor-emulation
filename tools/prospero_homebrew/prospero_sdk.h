#ifndef RAZOR_PROSPERO_HOMEBREW_SDK_H
#define RAZOR_PROSPERO_HOMEBREW_SDK_H

#include <stddef.h>
#include <stdint.h>

// Export library/module anchor strings into .dynstr so build_store_packages.py
// can reference their exact string-table offsets in DT_OS_IMPORT_LIB / DT_OS_NEEDED_MODULE.
extern "C" {
__attribute__((visibility("default"), used)) extern const char libkernel   = 0;
__attribute__((visibility("default"), used)) extern const char VideoOut    = 0;
__attribute__((visibility("default"), used)) extern const char Pad         = 0;
__attribute__((visibility("default"), used)) extern const char UserService = 0;

struct Ps5VideoOutBufferAttribute2 {
	uint32_t reserved0;
	uint32_t tiling_mode;
	uint32_t aspect_ratio;
	uint32_t width;
	uint32_t height;
	uint32_t pitch_in_pixel;
	uint64_t option;
	uint64_t pixel_format;
	uint64_t dcc_cb_register_clear_color;
	uint32_t dcc_control;
	uint32_t pad0;
	uint64_t reserved1[3];
};

struct Ps5VideoOutBuffers {
	const void* data;
	const void* metadata;
	const void* reserved[2];
};

struct Ps5VideoOutOutputStatus {
	uint32_t resolution;
	uint32_t dynamic_range;
	uint64_t refresh_rate;
	uint64_t flags;
	uint64_t reserved[3];
};

struct Ps5PadTouch {
	uint16_t x;
	uint16_t y;
	uint8_t  id;
	uint8_t  reserve[3];
};

struct Ps5PadTouchData {
	uint8_t     touch_num;
	uint8_t     reserve[3];
	uint32_t    reserve1;
	Ps5PadTouch touch[2];
};

struct Ps5PadData {
	uint32_t        buttons;
	uint8_t         left_stick_x;
	uint8_t         left_stick_y;
	uint8_t         right_stick_x;
	uint8_t         right_stick_y;
	uint8_t         analog_l2;
	uint8_t         analog_r2;
	uint8_t         padding[2];
	float           orientation[4];
	float           acceleration[3];
	float           angular_velocity[3];
	Ps5PadTouchData touch_data;
	bool            connected;
	uint64_t        timestamp;
	uint32_t        ext_unit_id;
	uint8_t         ext_reserve[1];
	uint8_t         ext_data_len;
	uint8_t         ext_data[10];
	uint8_t         connected_count;
	uint8_t         reserve[2];
	uint8_t         unique_data_len;
	uint8_t         unique_data[12];
};
static_assert(sizeof(Ps5PadData) == 120, "Ps5PadData must match 120-byte Prospero ABI");

struct Ps5LightBarColor {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t reserved;
};

// Placeholder symbol names (15 bytes each) rewritten in-place to "<NID>#<Lib>#<Mod>"
// by tools/prospero_homebrew/build_store_packages.py.
int  ps5_AllocDirectMem(size_t len, size_t align, int mem_type, int64_t* phys_out) __asm__("A_A_B+vc2AO2Zrc");
int  ps5_MapDirectMem(void** addr, size_t len, int prot, int flags, int64_t phys, size_t align) __asm__("A_A_L-Q3LEjIbgA");
int  ps5_Usleep(uint32_t usec) __asm__("A_A_1jfXLRVzisc");

int  ps5_VideoOutOpen(int user_id, int bus_type, int index, const void* param) __asm__("B_B_Up36PTk687E");
int  ps5_VideoOutAllowWqhd(int handle) __asm__("B_B_w7Ipp9Xl7hg");
int  ps5_VideoOutGetStatus(int handle, Ps5VideoOutOutputStatus* status) __asm__("B_B_utPrVdxio-8");
void ps5_VideoOutSetAttr2(Ps5VideoOutBufferAttribute2* attr, uint64_t pixel_format, uint32_t tiling_mode,
                          uint32_t width, uint32_t height, uint64_t option, uint32_t dcc_control,
                          uint64_t dcc_clear) __asm__("B_B_PjS5uASwcV8");
int  ps5_VideoOutRegBuffers2(int handle, int set_index, int buf_start, const Ps5VideoOutBuffers* bufs,
                             int buf_num, const Ps5VideoOutBufferAttribute2* attr, int category,
                             void* option) __asm__("B_B_rKBUtgRrtbk");
int  ps5_VideoOutSubmitFlip(int handle, int index, int flip_mode, int64_t flip_arg) __asm__("B_B_U46NwOiJpys");
int  ps5_VideoOutWaitVblank(int handle) __asm__("B_B_j6RaAUlaLv0");

int  ps5_PadInit(void) __asm__("C_C_hv1luiJrqQM");
int  ps5_PadOpen(int user_id, int type, int index, const void* param) __asm__("C_C_xk0AcarP3V4");
int  ps5_PadReadState(int handle, Ps5PadData* data) __asm__("C_C_YndgXqQVV7c");
int  ps5_PadSetLightBar(int handle, const Ps5LightBarColor* color) __asm__("C_C_RR4novUEENY");

int  ps5_UserServiceInit(const void* params) __asm__("D_D_j3YMu1MVNNo");
int  ps5_UserServiceGetInitialUser(int* user_id) __asm__("D_D_CdWp0oHWGr0");
}

constexpr uint32_t PS5_BTN_L3       = 0x00000002u;
constexpr uint32_t PS5_BTN_R3       = 0x00000004u;
constexpr uint32_t PS5_BTN_OPTIONS  = 0x00000008u;
constexpr uint32_t PS5_BTN_UP       = 0x00000010u;
constexpr uint32_t PS5_BTN_RIGHT    = 0x00000020u;
constexpr uint32_t PS5_BTN_DOWN     = 0x00000040u;
constexpr uint32_t PS5_BTN_LEFT     = 0x00000080u;
constexpr uint32_t PS5_BTN_L2       = 0x00000100u;
constexpr uint32_t PS5_BTN_R2       = 0x00000200u;
constexpr uint32_t PS5_BTN_L1       = 0x00000400u;
constexpr uint32_t PS5_BTN_R1       = 0x00000800u;
constexpr uint32_t PS5_BTN_TRIANGLE = 0x00001000u;
constexpr uint32_t PS5_BTN_CIRCLE   = 0x00002000u;
constexpr uint32_t PS5_BTN_CROSS    = 0x00004000u;
constexpr uint32_t PS5_BTN_SQUARE   = 0x00008000u;
constexpr uint32_t PS5_BTN_TOUCHPAD = 0x00100000u;

constexpr uint64_t PS5_PIXEL_FORMAT_B8G8R8A8_SRGB = 0x8000000000000000ULL;

constexpr uint32_t MakeArgb(uint8_t r, uint8_t g, uint8_t b) {
	return 0xFF000000u | (static_cast<uint32_t>(r) << 16u) | (static_cast<uint32_t>(g) << 8u) |
	       static_cast<uint32_t>(b);
}

// Gunmetal Grey Luxury palette constants
constexpr uint32_t COL_BG_DEEP      = MakeArgb(18, 20, 24);
constexpr uint32_t COL_BG_PANEL     = MakeArgb(28, 32, 39);
constexpr uint32_t COL_BG_ELEVATED  = MakeArgb(39, 45, 55);
constexpr uint32_t COL_STEEL_BORDER = MakeArgb(68, 77, 92);
constexpr uint32_t COL_GOLD         = MakeArgb(214, 178, 108);
constexpr uint32_t COL_GOLD_BRIGHT  = MakeArgb(245, 214, 142);
constexpr uint32_t COL_SILVER       = MakeArgb(228, 233, 240);
constexpr uint32_t COL_MUTED        = MakeArgb(148, 158, 174);
constexpr uint32_t COL_CYAN         = MakeArgb(76, 208, 238);
constexpr uint32_t COL_EMERALD      = MakeArgb(78, 209, 146);
constexpr uint32_t COL_CRIMSON      = MakeArgb(232, 88, 104);
constexpr uint32_t COL_VIOLET       = MakeArgb(174, 118, 242);

// Freestanding helpers for -nostdlib builds
extern "C" inline void* memset(void* dst, int c, size_t n) {
	auto* p = static_cast<uint8_t*>(dst);
	for (size_t i = 0; i < n; ++i) {
		p[i] = static_cast<uint8_t>(c);
	}
	return dst;
}

extern "C" inline void* memcpy(void* dst, const void* src, size_t n) {
	auto*       d = static_cast<uint8_t*>(dst);
	const auto* s = static_cast<const uint8_t*>(src);
	for (size_t i = 0; i < n; ++i) {
		d[i] = s[i];
	}
	return dst;
}

// Compact 5x7 uppercase/digit/symbol bitmap font (each entry = 5 columns of 7-bit rows, LSB=top)
inline const uint8_t* GetGlyph5x7(char c) {
	static const uint8_t SPACE[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
	static const uint8_t DIGITS[10][5] = {
	    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
	    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
	    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
	    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
	    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
	    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
	    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
	    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
	    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
	    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
	};
	static const uint8_t ALPHA[26][5] = {
	    {0x7E, 0x11, 0x11, 0x11, 0x7E}, // A
	    {0x7F, 0x49, 0x49, 0x49, 0x36}, // B
	    {0x3E, 0x41, 0x41, 0x41, 0x22}, // C
	    {0x7F, 0x41, 0x41, 0x22, 0x1C}, // D
	    {0x7F, 0x49, 0x49, 0x49, 0x41}, // E
	    {0x7F, 0x09, 0x09, 0x09, 0x01}, // F
	    {0x3E, 0x41, 0x49, 0x49, 0x7A}, // G
	    {0x7F, 0x08, 0x08, 0x08, 0x7F}, // H
	    {0x00, 0x41, 0x7F, 0x41, 0x00}, // I
	    {0x20, 0x40, 0x41, 0x3F, 0x01}, // J
	    {0x7F, 0x08, 0x14, 0x22, 0x41}, // K
	    {0x7F, 0x40, 0x40, 0x40, 0x40}, // L
	    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, // M
	    {0x7F, 0x04, 0x08, 0x10, 0x7F}, // N
	    {0x3E, 0x41, 0x41, 0x41, 0x3E}, // O
	    {0x7F, 0x09, 0x09, 0x09, 0x06}, // P
	    {0x3E, 0x41, 0x51, 0x21, 0x5E}, // Q
	    {0x7F, 0x09, 0x19, 0x29, 0x46}, // R
	    {0x46, 0x49, 0x49, 0x49, 0x31}, // S
	    {0x01, 0x01, 0x7F, 0x01, 0x01}, // T
	    {0x3F, 0x40, 0x40, 0x40, 0x3F}, // U
	    {0x1F, 0x20, 0x40, 0x20, 0x1F}, // V
	    {0x3F, 0x40, 0x38, 0x40, 0x3F}, // W
	    {0x63, 0x14, 0x08, 0x14, 0x63}, // X
	    {0x07, 0x08, 0x70, 0x08, 0x07}, // Y
	    {0x61, 0x51, 0x49, 0x45, 0x43}, // Z
	};
	static const uint8_t COLON[5]  = {0x00, 0x36, 0x36, 0x00, 0x00};
	static const uint8_t DASH[5]   = {0x08, 0x08, 0x08, 0x08, 0x08};
	static const uint8_t DOT[5]    = {0x00, 0x60, 0x60, 0x00, 0x00};
	static const uint8_t SLASH[5]  = {0x20, 0x10, 0x08, 0x04, 0x02};
	static const uint8_t PLUS[5]   = {0x08, 0x08, 0x3E, 0x08, 0x08};
	static const uint8_t EXCL[5]   = {0x00, 0x00, 0x5F, 0x00, 0x00};
	static const uint8_t PCT[5]    = {0x23, 0x13, 0x08, 0x64, 0x62};
	static const uint8_t LPAREN[5] = {0x00, 0x1C, 0x22, 0x41, 0x00};
	static const uint8_t RPAREN[5] = {0x00, 0x41, 0x22, 0x1C, 0x00};
	static const uint8_t STAR[5]   = {0x14, 0x08, 0x3E, 0x08, 0x14};

	if (c >= 'a' && c <= 'z') {
		c = static_cast<char>(c - 'a' + 'A');
	}
	if (c >= '0' && c <= '9') {
		return DIGITS[c - '0'];
	}
	if (c >= 'A' && c <= 'Z') {
		return ALPHA[c - 'A'];
	}
	switch (c) {
		case ':': return COLON;
		case '-': return DASH;
		case '.': return DOT;
		case '/': return SLASH;
		case '+': return PLUS;
		case '!': return EXCL;
		case '%': return PCT;
		case '(': return LPAREN;
		case ')': return RPAREN;
		case '*': return STAR;
		default: return SPACE;
	}
}

inline void FormatInt(char* out, int value) {
	if (value == 0) {
		out[0] = '0';
		out[1] = '\0';
		return;
	}
	bool neg = false;
	uint32_t u = 0;
	if (value < 0) {
		neg = true;
		u   = static_cast<uint32_t>(-value);
	} else {
		u = static_cast<uint32_t>(value);
	}
	char tmp[16];
	int  len = 0;
	while (u > 0 && len < 15) {
		tmp[len++] = static_cast<char>('0' + (u % 10u));
		u /= 10u;
	}
	int pos = 0;
	if (neg) {
		out[pos++] = '-';
	}
	while (len > 0) {
		out[pos++] = tmp[--len];
	}
	out[pos] = '\0';
}

inline int StrLen(const char* s) {
	int n = 0;
	while (s[n] != '\0') {
		++n;
	}
	return n;
}

inline void StrAppend(char* dst, const char* src) {
	int d = StrLen(dst);
	int i = 0;
	while (src[i] != '\0') {
		dst[d++] = src[i++];
	}
	dst[d] = '\0';
}

struct ProsperoRuntime {
	int      video_handle  = -1;
	int      pad_handle    = -1;
	int      user_id       = 1;
	uint32_t width         = 1920;
	uint32_t height        = 1080;
	uint32_t res_code      = 1; // 1=1080p, 3=1440p, 2=4K
	uint32_t current_buf   = 0;
	int64_t  frame_counter = 0;

	uint32_t* buffers[2]   = {nullptr, nullptr};
	uint32_t  x_lut[3840]  = {};
	uint32_t  y_lut[2160]  = {};

	bool Init() {
		ps5_UserServiceInit(nullptr);
		if (ps5_UserServiceGetInitialUser(&user_id) != 0) {
			user_id = 1;
		}

		video_handle = ps5_VideoOutOpen(user_id, 0, 0, nullptr);
		if (video_handle < 0) {
			return false;
		}

		// Enable PS5 SDK 6+ WQHD 1440p detection so RazorEmulations' PC Resolution selector
		// (1080p / 1440p / 4K) maps 1:1 to native Prospero surfaces!
		ps5_VideoOutAllowWqhd(video_handle);

		Ps5VideoOutOutputStatus status {};
		if (ps5_VideoOutGetStatus(video_handle, &status) == 0) {
			res_code = status.resolution;
			if (status.resolution == 3u) {
				width  = 2560;
				height = 1440;
			} else if (status.resolution == 2u) {
				width  = 3840;
				height = 2160;
			} else {
				width  = 1920;
				height = 1080;
			}
		}

		const uint32_t blocks_x  = (width + 127u) >> 7u;
		const uint32_t blocks_y  = (height + 127u) >> 7u;
		const size_t   surf_size = static_cast<size_t>(blocks_x) * blocks_y * 65536u;
		const size_t   alloc_len = (surf_size * 2u + 0xFFFFFu) & ~static_cast<size_t>(0xFFFFFu);

		int64_t phys_addr = 0;
		if (ps5_AllocDirectMem(alloc_len, 65536u, 3, &phys_addr) != 0) {
			return false;
		}
		void* mapped = nullptr;
		if (ps5_MapDirectMem(&mapped, alloc_len, 0x33, 0, phys_addr, 65536u) != 0 || mapped == nullptr) {
			return false;
		}

		auto* base = static_cast<uint8_t*>(mapped);
		buffers[0] = reinterpret_cast<uint32_t*>(base);
		buffers[1] = reinterpret_cast<uint32_t*>(base + surf_size);

		// Precompute Prospero TileMode::kRenderTarget (128x128 64KB block) LUTs:
		for (uint32_t x = 0; x < width; ++x) {
			const uint32_t bx = x >> 7u;
			const uint32_t sx = ((x << 2u) & 0x00cu) ^ ((x << 5u) & 0x380u) ^
			                    ((x << 4u) & 0x400u) ^ ((x << 6u) & 0x800u) ^ ((x << 9u) & 0xa000u);
			x_lut[x] = (bx * 65536u + sx) >> 2u;
		}
		for (uint32_t y = 0; y < height; ++y) {
			const uint32_t by = y >> 7u;
			const uint32_t sy = ((y << 4u) & 0x070u) ^ ((y << 5u) & 0xf00u) ^
			                    ((y << 9u) & 0x1000u) ^ ((y << 8u) & 0x4000u);
			y_lut[y] = (by * blocks_x * 65536u + sy) >> 2u;
		}

		Ps5VideoOutBufferAttribute2 attr {};
		ps5_VideoOutSetAttr2(&attr, PS5_PIXEL_FORMAT_B8G8R8A8_SRGB, 0u, width, height, 0u, 0u, 0u);

		Ps5VideoOutBuffers reg_bufs[2] {};
		reg_bufs[0].data = buffers[0];
		reg_bufs[1].data = buffers[1];
		if (ps5_VideoOutRegBuffers2(video_handle, 0, 0, reg_bufs, 2, &attr, 0, nullptr) != 0) {
			return false;
		}

		ps5_PadInit();
		pad_handle = ps5_PadOpen(user_id, 0, 0, nullptr);
		if (pad_handle >= 0) {
			const Ps5LightBarColor gold {214, 178, 108, 0};
			ps5_PadSetLightBar(pad_handle, &gold);
		}
		return true;
	}

	const char* ResolutionLabel() const {
		if (width == 3840) {
			return "4K UHD (3840X2160)";
		}
		if (width == 2560) {
			return "1440P WQHD (2560X1440)";
		}
		return "1080P FHD (1920X1080)";
	}

	// Coordinate helpers mapping virtual 1920x1080 space cleanly onto 1080p / 1440p / 4K
	int SX(int vx) const { return static_cast<int>((static_cast<int64_t>(vx) * width) / 1920); }
	int SY(int vy) const { return static_cast<int>((static_cast<int64_t>(vy) * height) / 1080); }

	void Clear(uint32_t top_col, uint32_t bot_col) {
		uint32_t* dst = buffers[current_buf];
		const int tr = static_cast<int>((top_col >> 16u) & 0xFFu);
		const int tg = static_cast<int>((top_col >> 8u) & 0xFFu);
		const int tb = static_cast<int>(top_col & 0xFFu);
		const int br = static_cast<int>((bot_col >> 16u) & 0xFFu);
		const int bg = static_cast<int>((bot_col >> 8u) & 0xFFu);
		const int bb = static_cast<int>(bot_col & 0xFFu);

		for (uint32_t y = 0; y < height; ++y) {
			const int      r   = tr + ((br - tr) * static_cast<int>(y)) / static_cast<int>(height);
			const int      g   = tg + ((bg - tg) * static_cast<int>(y)) / static_cast<int>(height);
			const int      b   = tb + ((bb - tb) * static_cast<int>(y)) / static_cast<int>(height);
			const uint32_t col = MakeArgb(static_cast<uint8_t>(r), static_cast<uint8_t>(g),
			                              static_cast<uint8_t>(b));
			const uint32_t row = y_lut[y];
			for (uint32_t x = 0; x < width; ++x) {
				dst[row + x_lut[x]] = col;
			}
		}
	}

	void FillRectV(int vx, int vy, int vw, int vh, uint32_t color) {
		int x0 = SX(vx);
		int y0 = SY(vy);
		int x1 = SX(vx + vw);
		int y1 = SY(vy + vh);
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 > static_cast<int>(width)) x1 = static_cast<int>(width);
		if (y1 > static_cast<int>(height)) y1 = static_cast<int>(height);
		if (x0 >= x1 || y0 >= y1) return;

		uint32_t* dst = buffers[current_buf];
		for (int y = y0; y < y1; ++y) {
			const uint32_t row = y_lut[y];
			for (int x = x0; x < x1; ++x) {
				dst[row + x_lut[x]] = color;
			}
		}
	}

	void DrawBorderV(int vx, int vy, int vw, int vh, int thick, uint32_t color) {
		FillRectV(vx, vy, vw, thick, color);
		FillRectV(vx, vy + vh - thick, vw, thick, color);
		FillRectV(vx, vy, thick, vh, color);
		FillRectV(vx + vw - thick, vy, thick, vh, color);
	}

	void FillCircleV(int vcx, int vcy, int vr, uint32_t color) {
		const int cx = SX(vcx);
		const int cy = SY(vcy);
		const int r  = SY(vr);
		if (r <= 0) return;
		int y0 = cy - r;
		int y1 = cy + r;
		if (y0 < 0) y0 = 0;
		if (y1 >= static_cast<int>(height)) y1 = static_cast<int>(height) - 1;

		uint32_t* dst = buffers[current_buf];
		const int r2  = r * r;
		for (int y = y0; y <= y1; ++y) {
			const int dy  = y - cy;
			const int dy2 = dy * dy;
			int       x0  = cx - r;
			int       x1  = cx + r;
			if (x0 < 0) x0 = 0;
			if (x1 >= static_cast<int>(width)) x1 = static_cast<int>(width) - 1;
			const uint32_t row = y_lut[y];
			for (int x = x0; x <= x1; ++x) {
				const int dx = x - cx;
				if (dx * dx + dy2 <= r2) {
					dst[row + x_lut[x]] = color;
				}
			}
		}
	}

	void DrawTextV(int vx, int vy, int vscale, const char* text, uint32_t color) {
		int cur_x = vx;
		for (int i = 0; text[i] != '\0'; ++i) {
			const uint8_t* g = GetGlyph5x7(text[i]);
			for (int col = 0; col < 5; ++col) {
				const uint8_t bits = g[col];
				if (bits == 0) continue;
				for (int row = 0; row < 7; ++row) {
					if ((bits & (1u << row)) != 0) {
						FillRectV(cur_x + col * vscale, vy + row * vscale, vscale, vscale, color);
					}
				}
			}
			cur_x += 6 * vscale;
		}
	}

	void DrawCenteredTextV(int vcx, int vy, int vscale, const char* text, uint32_t color) {
		const int w = StrLen(text) * 6 * vscale;
		DrawTextV(vcx - w / 2, vy, vscale, text, color);
	}

	void ReadPad(Ps5PadData* out) {
		memset(out, 0, sizeof(Ps5PadData));
		out->left_stick_x  = 128;
		out->left_stick_y  = 128;
		out->right_stick_x = 128;
		out->right_stick_y = 128;
		if (pad_handle >= 0) {
			ps5_PadReadState(pad_handle, out);
		}
	}

	void Present() {
		ps5_VideoOutSubmitFlip(video_handle, static_cast<int>(current_buf), 1, ++frame_counter);
		ps5_VideoOutWaitVblank(video_handle);
		current_buf ^= 1u;
	}
};

#endif // RAZOR_PROSPERO_HOMEBREW_SDK_H

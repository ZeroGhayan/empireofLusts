#include "exo/platform.h"

#include <3ds.h>
#include <citro2d.h>
#include <string.h>

#define STICK_MAX 154.0f
#define DEAD      20

static C3D_RenderTarget *g_left;
static C3D_RenderTarget *g_right;
static C2D_TextBuf g_tbuf;
static bool  g_new3ds;
static bool  g_romfs;
static u64   g_t0, g_t1;
static float g_dt;
static float g_slider;
static circlePosition g_stick, g_cstick;
static touchPosition  g_touch;
static ExoInput g_in;
static u8 *g_botfb;

static float axis_from(s16 v)
{
	if (v > -DEAD && v < DEAD)
		return 0.0f;
	return (float)v / STICK_MAX;
}

static u32 map_keys(u32 hid)
{
	u32 m = 0;
	if (hid & KEY_A)      m |= EXO_BTN_A;
	if (hid & KEY_B)      m |= EXO_BTN_B;
	if (hid & KEY_X)      m |= EXO_BTN_X;
	if (hid & KEY_Y)      m |= EXO_BTN_Y;
	if (hid & KEY_L)      m |= EXO_BTN_L;
	if (hid & KEY_R)      m |= EXO_BTN_R;
	if (hid & KEY_ZL)     m |= EXO_BTN_ZL;
	if (hid & KEY_ZR)     m |= EXO_BTN_ZR;
	if (hid & KEY_START)  m |= EXO_BTN_START;
	if (hid & KEY_SELECT) m |= EXO_BTN_SELECT;
	if (hid & KEY_DLEFT)  m |= EXO_BTN_LEFT;
	if (hid & KEY_DRIGHT) m |= EXO_BTN_RIGHT;
	if (hid & KEY_DUP)    m |= EXO_BTN_UP;
	if (hid & KEY_DDOWN)  m |= EXO_BTN_DOWN;
	return m;
}

bool exo_init(void)
{
	gfxInitDefault();
    gfxSetDoubleBuffering(GFX_BOTTOM, false);
	gfxSet3D(true);
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	C2D_Init(C2D_DEFAULT_MAX_OBJECTS);
	C2D_Prepare();

	g_left  = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	g_right = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
	if (!g_left || !g_right)
		return false;

	g_tbuf = C2D_TextBufNew(512);
	g_romfs = (romfsInit() == 0);
	APT_CheckNew3DS(&g_new3ds);
	g_t0 = osGetTime();
	g_t1 = g_t0;
	g_dt = 1.0f / 60.0f;
	g_botfb = NULL;
	memset(&g_in, 0, sizeof(g_in));
	g_in.new_3ds = g_new3ds;
	return true;
}

void exo_shutdown(void)
{
	if (g_tbuf) {
		C2D_TextBufDelete(g_tbuf);
		g_tbuf = NULL;
	}
	C2D_Fini();
	C3D_Fini();
	if (g_romfs) {
		romfsExit();
		g_romfs = false;
	}
	gfxExit();
}

bool exo_frame_begin(void)
{
	if (!aptMainLoop())
		return false;

	hidScanInput();
	hidCircleRead(&g_stick);
	hidTouchRead(&g_touch);
    g_in.touch_x     = g_touch.px;
	g_in.touch_y     = g_touch.py;
	g_in.touch_press = (hidKeysDown() & KEY_TOUCH) ? 1 : 0;
	if (g_new3ds)
		hidCstickRead(&g_cstick);
	else {
		g_cstick.dx = 0;
		g_cstick.dy = 0;
	}

	g_slider = osGet3DSliderState();
	g_t1 = osGetTime();
	g_dt = (float)(g_t1 - g_t0) / 1000.0f;
	if (g_dt <= 0.0f || g_dt > 0.05f)
		g_dt = 1.0f / 60.0f;
	g_t0 = g_t1;

    g_in.held     = map_keys(hidKeysHeld());
	g_in.down     = map_keys(hidKeysDown());
	g_in.up       = map_keys(hidKeysUp());
	g_in.stick_x  = axis_from(g_stick.dx);
	g_in.stick_y  = axis_from(g_stick.dy);
	g_in.cstick_x = axis_from(g_cstick.dx);
	g_in.cstick_y = axis_from(g_cstick.dy);
	g_in.slider_3d = g_slider;
	g_in.new_3ds  = g_new3ds;
	return true;
}

void exo_frame_end(void)
{
}

const ExoInput *exo_input(void) { return &g_in; }
bool  exo_down(u32 m) { return (g_in.down & m) != 0; }
bool  exo_held(u32 m) { return (g_in.held & m) != 0; }
bool  exo_up(u32 m)   { return (g_in.up & m) != 0; }
float exo_dt(void)    { return g_dt; }
float exo_slider_3d(void) { return g_slider; }

void exo_render_begin(void)
{
	C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
	C2D_Prepare();
}

void exo_render_eye(ExoEye eye, u32 clear_rgba)
{
	C3D_RenderTarget *t = (eye == EXO_EYE_LEFT) ? g_left : g_right;
	C2D_TargetClear(t, clear_rgba);
	C2D_SceneBegin(t);
}

void exo_render_end(void)
{
	if (g_botfb)
		GSPGPU_FlushDataCache(g_botfb, 320 * 240 * 3);
	C3D_FrameEnd(0);
}

float exo_parallax(float depth, ExoEye eye)
{
	float sign = (eye == EXO_EYE_LEFT) ? -1.0f : 1.0f;
	return sign * g_slider * depth;
}

void exo_top_text(float x, float y, float scale, uint32_t rgba, const char *s)
{
	C2D_Text t;

	if (!g_tbuf || !s || !s[0])
		return;
	C2D_TextBufClear(g_tbuf);
	C2D_TextParse(&t, g_tbuf, s);
	C2D_TextOptimize(&t);
	C2D_DrawText(&t, C2D_WithColor | C2D_AlignCenter, x, y, 0.5f,
	             scale, scale, rgba);
}

static void bot_px(int x, int y, u8 r, u8 g, u8 b)
{
	u32 off;
	if (!g_botfb || x < 0 || y < 0 || x >= 320 || y >= 240)
		return;
	/* framebuffer de baixo: 240x320, BGR8, rodado */
	off = ((u32)x * 240u + (u32)(239 - y)) * 3u;
	g_botfb[off + 0] = b;
	g_botfb[off + 1] = g;
	g_botfb[off + 2] = r;
}

static void unpack(u32 rgba, u8 *r, u8 *g, u8 *b)
{
	*r = (u8)(rgba & 0xFFu);
	*g = (u8)((rgba >> 8) & 0xFFu);
	*b = (u8)((rgba >> 16) & 0xFFu);
}

void exo_render_bottom(uint32_t clear_rgba)
{
	u8 r, g, b;
	int i, n;

	g_botfb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, NULL, NULL);
	if (!g_botfb)
		return;
	unpack(clear_rgba, &r, &g, &b);
	n = 320 * 240;
	for (i = 0; i < n; ++i) {
		g_botfb[i * 3 + 0] = b;
		g_botfb[i * 3 + 1] = g;
		g_botfb[i * 3 + 2] = r;
	}
}

void exo_bot_rect(float x, float y, float w, float h, uint32_t rgba)
{
	int x0 = (int)x, y0 = (int)y;
	int x1 = (int)(x + w), y1 = (int)(y + h);
	int ix, iy;
	u8 r, g, b;

	unpack(rgba, &r, &g, &b);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > 320) x1 = 320;
	if (y1 > 240) y1 = 240;
	for (iy = y0; iy < y1; ++iy)
		for (ix = x0; ix < x1; ++ix)
			bot_px(ix, iy, r, g, b);
}

static const uint8_t FONT[59][7] = {
	{0,0,0,0,0,0,0},{4,4,4,4,0,4,0},{10,10,0,0,0,0,0},{10,31,10,31,10,0,0},
	{4,15,20,14,5,30,4},{18,18,4,8,18,18,0},{8,20,8,21,18,13,0},{4,4,0,0,0,0,0},
	{2,4,4,4,4,2,0},{8,4,4,4,4,8,0},{0,10,4,31,4,10,0},{0,4,4,31,4,4,0},
	{0,0,0,0,4,4,8},{0,0,0,31,0,0,0},{0,0,0,0,0,4,0},{1,2,4,8,16,0,0},
	{14,17,19,21,25,14,0},{4,12,4,4,4,14,0},{14,17,2,4,8,31,0},{14,17,6,1,17,14,0},
	{2,6,10,31,2,2,0},{31,16,30,1,17,14,0},{14,16,30,17,17,14,0},{31,1,2,4,8,8,0},
	{14,17,14,17,17,14,0},{14,17,17,15,1,14,0},{0,4,0,0,4,0,0},{0,4,0,0,4,4,8},
	{2,4,8,4,2,0,0},{0,0,31,0,31,0,0},{8,4,2,4,8,0,0},{14,17,2,4,0,4,0},
	{14,17,23,21,16,14,0},{14,17,17,31,17,17,0},{30,17,30,17,17,30,0},
	{14,17,16,16,17,14,0},{30,17,17,17,17,30,0},{31,16,30,16,16,31,0},
	{31,16,30,16,16,16,0},{14,17,16,23,17,14,0},{17,17,31,17,17,17,0},
	{14,4,4,4,4,14,0},{1,1,1,1,17,14,0},{17,18,28,18,17,17,0},
	{16,16,16,16,16,31,0},{17,27,21,17,17,17,0},{17,25,21,19,17,17,0},
	{14,17,17,17,17,14,0},{30,17,17,30,16,16,0},{14,17,17,21,18,13,0},
	{30,17,17,30,18,17,0},{14,17,8,4,17,14,0},{31,4,4,4,4,4,0},
	{17,17,17,17,17,14,0},{17,17,17,17,10,4,0},{17,17,17,21,27,17,0},
	{17,17,14,17,17,17,0},{17,17,10,4,4,4,0},{31,1,2,4,8,31,0},
};

void exo_text_begin(void)
{
}

void exo_text(float x, float y, float scale, uint32_t rgba, const char *s)
{
	int px = (scale < 0.6f) ? 2 : 3;
	int cx = (int)x, i, row, col;
	u8 r, g, b;

	unpack(rgba, &r, &g, &b);
	if (!s)
		return;
	for (i = 0; s[i]; ++i) {
		int ch = s[i];
		if (ch >= 'a' && ch <= 'z')
			ch -= 32;
		if (ch == '_')
			ch = '-';
		if (ch < 32 || ch > 90) {
			cx += 6 * px;
			continue;
		}
		for (row = 0; row < 7; ++row) {
			u8 bits = FONT[ch - 32][row];
			for (col = 0; col < 5; ++col)
				if (bits & (1 << (4 - col))) {
					int p, q;
					for (q = 0; q < px; ++q)
						for (p = 0; p < px; ++p)
							bot_px(cx + col * px + p,
							       (int)y + row * px + q, r, g, b);
				}
		}
		cx += 6 * px;
	}
}

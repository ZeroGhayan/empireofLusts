#include "exo/platform.h"

#include <3ds.h>
#include <citro2d.h>
#include <citro3d.h>
#include <string.h>
#include <stdlib.h>

#define STICK_MAX 154.0f
#define DEAD      20

static C3D_RenderTarget *g_left;
static C3D_RenderTarget *g_right;
static C3D_RenderTarget *g_bot;
static C2D_TextBuf g_tbuf;
static bool  g_new3ds;
static bool  g_romfs;
static u64   g_t0, g_t1;
static float g_dt;
static float g_slider;
static circlePosition g_stick, g_cstick;
static touchPosition  g_touch;
static ExoInput g_in;
static C3D_Tex g_mini_tex;
static Tex3DS_SubTexture g_mini_sub;
static C2D_Image g_mini_img;
static int g_mini_ready;
static int g_mini_w, g_mini_h;

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
	gfxSet3D(true);
	C3D_Init(C3D_DEFAULT_CMDBUF_SIZE);
	/* dois olhos + HUD; o default 4096 rebenta com minimapa a pontos */
	C2D_Init(8192);
	C2D_Prepare();

	g_left  = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
	g_right = C2D_CreateScreenTarget(GFX_TOP, GFX_RIGHT);
	g_bot   = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
	if (!g_left || !g_right || !g_bot)
		return false;
	g_mini_ready = 0;
	g_mini_w = g_mini_h = 0;

	g_tbuf = C2D_TextBufNew(512);
	g_romfs = (romfsInit() == 0);
	APT_CheckNew3DS(&g_new3ds);
	g_t0 = osGetTime();
	g_t1 = g_t0;
	g_dt = 1.0f / 60.0f;
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
	if (g_mini_ready) {
		C3D_TexDelete(&g_mini_tex);
		g_mini_ready = 0;
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
	g_in.touch_held  = (hidKeysHeld() & KEY_TOUCH) ? 1 : 0;
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

void exo_render_bottom(uint32_t clear_rgba)
{
	if (!g_bot)
		return;
	C2D_TargetClear(g_bot, clear_rgba);
	C2D_SceneBegin(g_bot);
}

void exo_bot_dot(int x, int y, uint32_t rgba)
{
	C2D_DrawRectSolid((float)x, (float)y, 0.45f, 1.0f, 1.0f, rgba);
}

void exo_bot_rect(float x, float y, float w, float h, uint32_t rgba)
{
	if (w < 1.0f) w = 1.0f;
	if (h < 1.0f) h = 1.0f;
	C2D_DrawRectSolid(x, y, 0.44f, w, h, rgba);
}

void exo_bot_line(float x0, float y0, float x1, float y1, uint32_t rgba)
{
	C2D_DrawLine(x0, y0, rgba, x1, y1, rgba, 1.5f, 0.46f);
}

void exo_text_begin(void)
{
}

void exo_text(float x, float y, float scale, uint32_t rgba, const char *s)
{
	C2D_Text t;

	if (!g_tbuf || !s || !s[0])
		return;
	C2D_TextBufClear(g_tbuf);
	C2D_TextParse(&t, g_tbuf, s);
	C2D_TextOptimize(&t);
	C2D_DrawText(&t, C2D_WithColor, x, y, 0.55f, scale, scale, rgba);
}

static u32 mini_tile_off(u32 x, u32 y, u32 w)
{
	u32 cx = x >> 3, cy = y >> 3;
	u32 px = x & 7, py = y & 7;
	u32 tile = (cy * (w >> 3) + cx) * 64u;
	u32 local = (px & 1u)
	          | ((py & 1u) << 1)
	          | ((px & 2u) << 1)
	          | ((py & 2u) << 2)
	          | ((px & 4u) << 2)
	          | ((py & 4u) << 3);
	return tile + local;
}

static u16 rgba_to_565(uint32_t rgba)
{
	u32 r = rgba & 0xFFu;
	u32 g = (rgba >> 8) & 0xFFu;
	u32 b = (rgba >> 16) & 0xFFu;
	return (u16)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void exo_minimap_begin(int w, int h)
{
	if (w < 8) w = 8;
	if (h < 8) h = 8;
	if (w > 128) w = 128;
	if (h > 128) h = 128;
	if (!g_mini_ready) {
		if (!C3D_TexInit(&g_mini_tex, 128, 128, GPU_RGB565))
			return;
		C3D_TexSetFilter(&g_mini_tex, GPU_NEAREST, GPU_NEAREST);
		g_mini_sub.width = 128;
		g_mini_sub.height = 128;
		g_mini_sub.left = 0.0f;
		g_mini_sub.top = 1.0f;
		g_mini_sub.right = 1.0f;
		g_mini_sub.bottom = 0.0f;
		g_mini_img.tex = &g_mini_tex;
		g_mini_img.subtex = &g_mini_sub;
		g_mini_ready = 1;
	}
	g_mini_w = w;
	g_mini_h = h;
	memset(g_mini_tex.data, 0, 128 * 128 * 2);
}

void exo_minimap_put(int x, int y, uint32_t rgba)
{
	u16 *px;
	if (!g_mini_ready || x < 0 || y < 0 || x >= 128 || y >= 128)
		return;
	px = (u16 *)g_mini_tex.data;
	px[mini_tile_off((u32)x, (u32)y, 128)] = rgba_to_565(rgba);
}

void exo_minimap_end(void)
{
	if (g_mini_ready)
		C3D_TexFlush(&g_mini_tex);
}

void exo_minimap_draw(float x, float y, float s)
{
	C2D_DrawParams par;
	if (!g_mini_ready)
		return;
	memset(&par, 0, sizeof(par));
	par.pos.x = x;
	par.pos.y = y;
	par.pos.w = s;
	par.pos.h = s;
	par.depth = 0.42f;
	C2D_DrawImage(g_mini_img, &par, NULL);
}

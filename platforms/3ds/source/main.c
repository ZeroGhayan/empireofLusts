#include "exo/platform.h"
#include "exo/flight.h"
#include "exo/tilemap.h"

#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define RGB32(r, g, b) \
	((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | (255u << 24))

#define COL_SKY    RGB32(18, 28, 64)
#define COL_BOT    RGB32(12, 12, 20)
#define COL_TEXT   RGB32(240, 240, 240)
#define COL_DIM    RGB32(160, 170, 190)
#define COL_ACCENT RGB32(255, 210, 64)
#define COL_PAD    RGB32(80, 200, 255)
#define COL_HEAD   RGB32(255, 210, 64)
#define COL_SLIDE  RGB32(70, 200, 120)

static const u32 TILE_COL[] = {
	RGB32( 46, 110,  58),
	RGB32( 28,  28,  32),
	RGB32( 58,  62,  74),
	RGB32(210, 200,  70),
	RGB32( 90,  86,  70),
	RGB32( 40,  90, 140),
	RGB32(160,  70,  50),
	RGB32( 80, 160,  90)
};

static ExoFlight g_flight;
static C2D_SpriteSheet g_bg[3];
static C2D_SpriteSheet g_shi;
static C2D_SpriteSheet g_rex;
static bool g_have_bg[3];
static bool g_paused;
static float g_fps = 60.0f;

static u32 tile_color(uint16_t id)
{
	unsigned n = (unsigned)(sizeof(TILE_COL) / sizeof(TILE_COL[0]));
	return TILE_COL[id % n];
}

static void try_load_etm(void)
{
	FILE *fp;
	uint8_t *buf;
	long sz;

	fp = fopen("romfs:/flight/map.etm", "rb");
	if (!fp)
		return;
	fseek(fp, 0, SEEK_END);
	sz = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (sz <= 16 || sz > 128 * 128 * 2 + 64) {
		fclose(fp);
		return;
	}
	buf = (uint8_t *)malloc((size_t)sz);
	if (!buf) {
		fclose(fp);
		return;
	}
	if (fread(buf, 1, (size_t)sz, fp) == (size_t)sz)
		exo_tilemap_load(&g_flight.map, buf, (uint32_t)sz);
	free(buf);
	fclose(fp);
}

static void load_gfx(void)
{
	unsigned i;
	static const char *bgpath[3] = {
		"romfs:/gfx/bg0.t3x",
		"romfs:/gfx/bg1.t3x",
		"romfs:/gfx/bg2.t3x"
	};

	for (i = 0; i < 3; ++i) {
		g_bg[i] = C2D_SpriteSheetLoad(bgpath[i]);
		g_have_bg[i] = g_bg[i] != NULL;
	}
	g_shi = C2D_SpriteSheetLoad("romfs:/gfx/shirammy.t3x");
	g_rex = C2D_SpriteSheetLoad("romfs:/gfx/rexxi.t3x");
}

static void draw_layers(ExoEye eye, const ExoFlight *f)
{
	unsigned i;
	float scroll = f->yaw * 40.0f + f->x * 0.02f;
	float depths[3] = { 4.0f, 10.0f, 18.0f };
	static const u32 fallback[3] = {
		RGB32(22, 36, 80),
		RGB32(70, 50, 110),
		RGB32(40, 90, 130)
	};

	for (i = 0; i < 3; ++i) {
		float par = exo_parallax(depths[i], eye);
		int ix = (int)(-scroll * (0.15f + 0.25f * (float)i) + par);
		float x = (float)((ix % 400 + 400) % 400);
		if (g_have_bg[i]) {
			C2D_Image img = C2D_SpriteSheetGetImage(g_bg[i], 0);
			C2D_DrawImageAt(img, x - 400.0f, 0.0f, 0.1f + 0.05f * (float)i, NULL, 1.0f, 1.0f);
			C2D_DrawImageAt(img, x, 0.0f, 0.1f + 0.05f * (float)i, NULL, 1.0f, 1.0f);
		} else {
			float y0 = 8.0f + (float)i * 22.0f;
			C2D_DrawRectSolid(x - 400.0f, y0, 0.12f + 0.04f * (float)i,
			                  800.0f, 36.0f - (float)i * 6.0f, fallback[i]);
		}
	}
}

static void draw_poly(const float *s, const float *t, int n, u32 col)
{
	int k;
	if (n < 3)
		return;
	for (k = 1; k < n - 1; ++k)
		C2D_DrawTriangle(s[0], t[0], col, s[k], t[k], col, s[k + 1], t[k + 1], col, 0.3f);
}

static int quad_sane(const float *sx, const float *sy, int n, float horizon)
{
	int i, on = 0, sky = 0;
	float minx =  9999.0f, maxx = -9999.0f;
	float miny =  9999.0f, maxy = -9999.0f;

	(void)horizon;
	for (i = 0; i < n; ++i) {
		if (sx[i] < minx) minx = sx[i];
		if (sx[i] > maxx) maxx = sx[i];
		if (sy[i] < miny) miny = sy[i];
		if (sy[i] > maxy) maxy = sy[i];
		if (sx[i] > -60.0f && sx[i] < 460.0f &&
		    sy[i] > -30.0f && sy[i] < 270.0f)
			on++;
		if (sy[i] < -80.0f)
			sky++;
	}
	if (on == 0)
		return 0;
	if (sky == n)
		return 0;
	if (maxx < -40.0f || minx > 440.0f)
		return 0;
	if (maxy < -20.0f || miny > 250.0f)
		return 0;
	return 1;
}

static int emit_tile(ExoFlight *f, float ox, int tx, int tz,
                     int *drawn, int *culled, int cap, float far_z)
{
	float sx[6], sy[6];
	int nv = 0;
	float wx, wz, lz;

	if (*drawn >= cap)
		return 0;
	if (tx < 0 || tz < 0 || tx >= (int)f->map.w || tz >= (int)f->map.h) {
		(*culled)++;
		return 1;
	}
	wx = (float)tx * EXO_FLIGHT_CELL;
	wz = (float)tz * EXO_FLIGHT_CELL;
	if (!exo_flight_tile_visible(f, wx, wz, EXO_FLIGHT_CELL, ox, far_z, &lz)) {
		(*culled)++;
		return 1;
	}
	if (!exo_flight_clip_quad(f, wx, wz, EXO_FLIGHT_CELL, ox, sx, sy, &nv)) {
		(*culled)++;
		return 1;
	}
	if (!quad_sane(sx, sy, nv, f->horizon)) {
		(*culled)++;
		return 1;
	}
	draw_poly(sx, sy, nv, tile_color(exo_tilemap_at(&f->map, tx, tz)));
	(*drawn)++;
	return 1;
}

static void draw_floor(ExoEye eye, ExoFlight *f)
{
	float ox, oy;
	int ccx, ccz;
	int ring, dx, dz, drawn = 0, culled = 0;
	int R = f->render_r;
	int cap = f->draw_cap;
	float far_z;

	if (R < EXO_FLIGHT_RENDER_MIN) R = EXO_FLIGHT_RENDER_MIN;
	if (R > EXO_FLIGHT_RENDER_MAX) R = EXO_FLIGHT_RENDER_MAX;
	if (cap < 40) cap = 40;

	ccx = (int)floorf(f->cam_x / EXO_FLIGHT_CELL);
	ccz = (int)floorf(f->cam_z / EXO_FLIGHT_CELL);
	far_z = (float)R * EXO_FLIGHT_CELL + 8.0f;

	exo_flight_eye_offset(f, exo_slider_3d(), (int)eye, &ox, &oy);
	(void)oy;

	for (ring = 0; ring <= R; ++ring) {
		for (dz = -ring; dz <= ring; ++dz) {
			for (dx = -ring; dx <= ring; ++dx) {
				int ax = dx < 0 ? -dx : dx;
				int az = dz < 0 ? -dz : dz;
				int cheb = ax > az ? ax : az;
				if (cheb != ring)
					continue;
				if (!emit_tile(f, ox, ccx + dx, ccz + dz,
				               &drawn, &culled, cap, far_z))
					goto done;
			}
		}
	}
done:
	f->tiles_drawn = drawn;
	f->tiles_culled = culled;
}

static int pilot_frame(const ExoFlight *f)
{
	if (f->y > 2.5f)
		return 3;
	if (f->mode == EXO_FLIGHT_HIGH)
		return 2;
	if (f->speed > 2.5f)
		return 1;
	return 0;
}

static void draw_pilot(const ExoFlight *f)
{
	C2D_SpriteSheet sheet;
	C2D_Image img;
	C2D_Sprite spr;
	float px, py, iw, ih;
	int frame = pilot_frame(f);
	size_t n;

	sheet = (f->pilot == EXO_PILOT_REXXI) ? g_rex : g_shi;
	if (!sheet)
		return;
	n = C2D_SpriteSheetCount(sheet);
	if (n == 0)
		return;
	if ((size_t)frame >= n)
		frame = 0;
	img = C2D_SpriteSheetGetImage(sheet, frame);
	iw = img.subtex ? img.subtex->width : 32.0f;
	ih = img.subtex ? img.subtex->height : 48.0f;

	if (!exo_flight_project(f, f->x, f->z, 0.0f, &px, &py)) {
		px = 200.0f;
		py = 198.0f;
	}
	if (py > 228.0f) py = 228.0f;
	if (py < 36.0f) py = 36.0f;

	C2D_SpriteFromSheet(&spr, sheet, frame);
	C2D_SpriteSetCenter(&spr, iw * 0.5f, ih);
	C2D_SpriteSetPos(&spr, px, py);
	if (f->cam_ref == EXO_CAM_SURFACE)
		C2D_SpriteSetRotation(&spr, f->bank);
	else
		C2D_SpriteSetRotation(&spr, 0.0f);
	C2D_DrawSprite(&spr);
}

static void draw_speed_veil(const ExoFlight *f)
{
	float frac = exo_flight_speed_frac(f);
	float a;
	u32 col;

	if (frac < 0.35f)
		return;
	a = (frac - 0.35f) / 0.65f;
	if (a > 1.0f) a = 1.0f;
	a = a * a;
	col = ((u32)(150.0f * a) << 24);
	C2D_DrawRectSolid(0.0f, 0.0f, 0.85f, 400.0f, 240.0f, col);
	C2D_DrawRectSolid(0.0f, 0.0f, 0.86f, 400.0f, 28.0f + 36.0f * a, col);
	C2D_DrawRectSolid(0.0f, 212.0f - 20.0f * a, 0.86f, 400.0f, 28.0f + 20.0f * a, col);
}

static void draw_charge_bar(const ExoFlight *f)
{
	float w = 72.0f * f->charge;
	u32 fill = (f->mode == EXO_FLIGHT_HIGH) ? RGB32(255, 90, 70) : RGB32(80, 200, 255);

	C2D_DrawRectSolid(8.0f, 224.0f, 0.88f, 74.0f, 8.0f, RGB32(20, 22, 32));
	if (w > 0.5f)
		C2D_DrawRectSolid(9.0f, 225.0f, 0.89f, w, 6.0f, fill);
}

static void draw_top_overlay(const ExoFlight *f)
{
	char line[32];

	exo_top_text(200.0f, 8.0f, 0.45f, COL_TEXT,
	             f->mode == EXO_FLIGHT_HIGH ? "HIGH" : "LOW");
	snprintf(line, sizeof(line), "FPS %.0f", (double)g_fps);
	exo_top_text(360.0f, 8.0f, 0.42f, COL_ACCENT, line);
	snprintf(line, sizeof(line), "H %.1f", (double)f->y);
	exo_top_text(360.0f, 220.0f, 0.42f, COL_TEXT, line);
}

static void draw_pad_graph(const ExoFlight *f)
{
	const float x0 = 214.0f, y0 = 8.0f, s = 90.0f;
	const float cx = x0 + s * 0.5f, cy = y0 + s * 0.5f;
	const float arm = 36.0f;
	float pdx = f->pad_x * arm;
	float pdy = -f->pad_y * arm;
	float hx = sinf(f->yaw) * arm;
	float hz = -cosf(f->yaw) * arm;
	float mx = f->wish_x * arm;
	float mz = -f->wish_z * arm;

	exo_bot_rect(x0, y0, s, s, RGB32(22, 24, 34));
	exo_bot_rect(x0, y0, s, 1.0f, COL_DIM);
	exo_bot_rect(x0, y0 + s - 1.0f, s, 1.0f, COL_DIM);
	exo_bot_rect(x0, y0, 1.0f, s, COL_DIM);
	exo_bot_rect(x0 + s - 1.0f, y0, 1.0f, s, COL_DIM);
	exo_bot_line(cx - arm, cy, cx + arm, cy, RGB32(50, 54, 70));
	exo_bot_line(cx, cy - arm, cx, cy + arm, RGB32(50, 54, 70));
	exo_bot_line(cx, cy, cx + hx, cy + hz, COL_HEAD);
	exo_bot_line(cx, cy, cx + mx, cy + mz, COL_SLIDE);
	exo_bot_line(cx, cy, cx + pdx, cy + pdy, COL_PAD);
	exo_bot_rect(cx + pdx - 2.0f, cy + pdy - 2.0f, 5.0f, 5.0f, COL_PAD);
	exo_text(x0, y0 + s + 2.0f, 0.35f, COL_PAD, "PAD");
	exo_text(x0 + 28.0f, y0 + s + 2.0f, 0.35f, COL_HEAD, "YAW");
	exo_text(x0 + 56.0f, y0 + s + 2.0f, 0.35f, COL_SLIDE, "MOV");
}

static void draw_slider(const char *label, float t, int selected,
                        float x, float y, float w)
{
	u32 bar = selected ? COL_ACCENT : RGB32(40, 44, 58);
	u32 fill = selected ? COL_SLIDE : RGB32(80, 100, 130);

	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	exo_text(x, y, 0.38f, selected ? COL_ACCENT : COL_DIM, label);
	exo_bot_rect(x, y + 12.0f, w, 8.0f, bar);
	exo_bot_rect(x, y + 12.0f, w * t, 8.0f, fill);
	exo_bot_rect(x + w * t - 2.0f, y + 10.0f, 4.0f, 12.0f, COL_TEXT);
}

static int touch_in(const ExoInput *in, float x, float y, float w, float h)
{
	if (!in || !in->touch_held)
		return 0;
	return (float)in->touch_x >= x && (float)in->touch_x <= x + w &&
	       (float)in->touch_y >= y && (float)in->touch_y <= y + h;
}

static void apply_tune(ExoFlight *f, const ExoInput *in)
{
	float rend_t;
	const float sx = 8.0f, sw = 196.0f;
	const float y1 = 168.0f;

	if (in && in->touch_press &&
	    (float)in->touch_x >= sx && (float)in->touch_x <= sx + sw &&
	    (float)in->touch_y >= 132.0f && (float)in->touch_y <= 160.0f) {
		f->cam_ref = (f->cam_ref == EXO_CAM_SURFACE) ? EXO_CAM_PILOT : EXO_CAM_SURFACE;
	}
	if (touch_in(in, sx, y1, sw, 28.0f)) {
		rend_t = ((float)in->touch_x - sx) / sw;
		if (rend_t < 0.0f) rend_t = 0.0f;
		if (rend_t > 1.0f) rend_t = 1.0f;
		f->render_r = EXO_FLIGHT_RENDER_MIN +
		              (int)(rend_t * (float)(EXO_FLIGHT_RENDER_MAX - EXO_FLIGHT_RENDER_MIN) + 0.5f);
	}

	if (exo_down(EXO_BTN_LEFT) || exo_down(EXO_BTN_RIGHT)) {
		int dir = exo_down(EXO_BTN_RIGHT) ? 1 : -1;
		f->render_r += dir;
		if (f->render_r < EXO_FLIGHT_RENDER_MIN)
			f->render_r = EXO_FLIGHT_RENDER_MIN;
		if (f->render_r > EXO_FLIGHT_RENDER_MAX)
			f->render_r = EXO_FLIGHT_RENDER_MAX;
	}

	if (exo_down(EXO_BTN_SELECT))
		f->render_r = EXO_FLIGHT_RENDER;
}

static void draw_hud(const ExoFlight *f)
{
	const ExoPilotStats *s = exo_pilot_stats(f->pilot);
	char line[64];
	float hud = exo_flight_hud_speed(f);
	float hud_max = exo_flight_hud_max(f);
	float bar;
	float vmax = exo_flight_vmax(f);
	float rend_t = (float)(f->render_r - EXO_FLIGHT_RENDER_MIN) /
	               (float)(EXO_FLIGHT_RENDER_MAX - EXO_FLIGHT_RENDER_MIN);

	exo_render_bottom(COL_BOT);
	exo_text_begin();
	snprintf(line, sizeof(line), "EMPIRE OF LUSTS");
	exo_text(8.0f, 8.0f, 0.48f, COL_ACCENT, line);
	snprintf(line, sizeof(line), "%s  %s", s->name,
	         f->mode == EXO_FLIGHT_HIGH ? "HIGH F-ZERO" : "LOW  3D");
	exo_text(8.0f, 26.0f, 0.42f, COL_TEXT, line);
	if (f->pilot == EXO_PILOT_REXXI)
		snprintf(line, sizeof(line), "SPD %4.2f %s", (double)hud, s->hud_unit);
	else
		snprintf(line, sizeof(line), "SPD %4.0f %s", (double)hud, s->hud_unit);
	exo_text(8.0f, 44.0f, 0.42f, COL_TEXT, line);
	snprintf(line, sizeof(line), "REAL %5.1f/%5.1f", (double)f->speed, (double)vmax);
	exo_text(8.0f, 62.0f, 0.38f, COL_DIM, line);
	snprintf(line, sizeof(line), "TILE %03d %03d", f->cell_x, f->cell_z);
	exo_text(8.0f, 80.0f, 0.38f, COL_DIM, line);
	snprintf(line, sizeof(line), "DRAW %d/%d CUT %d",
	         f->tiles_drawn, f->draw_cap, f->tiles_culled);
	exo_text(8.0f, 96.0f, 0.38f, COL_DIM, line);

	bar = hud_max > 0.0f ? hud / hud_max : 0.0f;
	if (bar < 0.0f) bar = 0.0f;
	if (bar > 1.0f) bar = 1.0f;
	exo_bot_rect(8.0f, 114.0f, 196.0f, 8.0f, RGB32(30, 30, 40));
	exo_bot_rect(8.0f, 114.0f, 196.0f * bar, 8.0f,
	             f->mode == EXO_FLIGHT_HIGH ? RGB32(255, 90, 70)
	                                       : RGB32(80, 180, 255));

	exo_text(8.0f, 132.0f, 0.38f, COL_ACCENT, "CAM REF");
	exo_text(8.0f, 146.0f, 0.38f, COL_TEXT,
	         f->cam_ref == EXO_CAM_SURFACE ? "SURFACE" : "PILOT");
	draw_slider("RENDER R", rend_t, 1, 8.0f, 168.0f, 196.0f);
	snprintf(line, sizeof(line), "%d", f->render_r);
	exo_text(170.0f, 168.0f, 0.38f, COL_TEXT, line);

	draw_pad_graph(f);

	if (f->mode == EXO_FLIGHT_HIGH || f->flying)
		exo_text(8.0f, 200.0f, 0.32f, COL_DIM, "Y SPEED  A BRAKE  B POP");
	else
		exo_text(8.0f, 200.0f, 0.32f, COL_DIM, "A SPEED  B JUMP  X SISTER");
	exo_text(8.0f, 214.0f, 0.32f, COL_DIM, "ZR CAM  DPAD R  TOUCH REF");
	exo_text(8.0f, 228.0f, 0.32f, COL_DIM, "SELECT R18  START PAUSE");
	if (g_paused)
		exo_text(214.0f, 220.0f, 0.45f, COL_ACCENT, "PAUSED");
}

int main(void)
{
	if (!exo_init())
		return 1;
	exo_flight_init(&g_flight, EXO_PILOT_SHIRAMMY);
	try_load_etm();
	load_gfx();
	while (exo_frame_begin()) {
		const ExoInput *in = exo_input();
		float dt = exo_dt();

		g_fps = g_fps * 0.90f + (dt > 0.0001f ? (1.0f / dt) : 60.0f) * 0.10f;

		if (exo_down(EXO_BTN_START))
			g_paused = !g_paused;
		if (exo_down(EXO_BTN_X)) {
			exo_flight_set_pilot(&g_flight,
				g_flight.pilot == EXO_PILOT_SHIRAMMY ? EXO_PILOT_REXXI : EXO_PILOT_SHIRAMMY);
		}
		apply_tune(&g_flight, in);
		if (!g_paused)
			exo_flight_tick(&g_flight, in, dt);
		exo_render_begin();
		exo_render_eye(EXO_EYE_LEFT, COL_SKY);
		draw_layers(EXO_EYE_LEFT, &g_flight);
		draw_floor(EXO_EYE_LEFT, &g_flight);
		draw_pilot(&g_flight);
		draw_speed_veil(&g_flight);
		draw_charge_bar(&g_flight);
		draw_top_overlay(&g_flight);
		if (exo_slider_3d() > 0.05f) {
			exo_render_eye(EXO_EYE_RIGHT, COL_SKY);
			draw_layers(EXO_EYE_RIGHT, &g_flight);
			draw_floor(EXO_EYE_RIGHT, &g_flight);
			draw_pilot(&g_flight);
			draw_speed_veil(&g_flight);
			draw_charge_bar(&g_flight);
			draw_top_overlay(&g_flight);
		}
		draw_hud(&g_flight);
		exo_render_end();
		exo_frame_end();
	}
	if (g_shi) C2D_SpriteSheetFree(g_shi);
	if (g_rex) C2D_SpriteSheetFree(g_rex);
	if (g_bg[0]) C2D_SpriteSheetFree(g_bg[0]);
	if (g_bg[1]) C2D_SpriteSheetFree(g_bg[1]);
	if (g_bg[2]) C2D_SpriteSheetFree(g_bg[2]);
	exo_shutdown();
	return 0;
}

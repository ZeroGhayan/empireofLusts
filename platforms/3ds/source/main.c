#include "exo/platform.h"
#include "exo/flight.h"
#include "exo/tilemap.h"

#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* citro2d C2D_Color32 e inline, nao constante — nao serve em static init */
#define RGB32(r, g, b) \
	((u32)(r) | ((u32)(g) << 8) | ((u32)(b) << 16) | (255u << 24))

#define COL_SKY    RGB32(18, 28, 64)
#define COL_BOT    RGB32(12, 12, 20)
#define COL_TEXT   RGB32(240, 240, 240)
#define COL_DIM    RGB32(160, 170, 190)
#define COL_ACCENT RGB32(255, 210, 64)

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
static C2D_SpriteSheet g_pilot;
static bool g_have_bg[3];
static bool g_have_pilot;
static bool g_paused;

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
	g_pilot = C2D_SpriteSheetLoad("romfs:/gfx/pilot.t3x");
	g_have_pilot = g_pilot != NULL;
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
		float x = (float)((int)(-scroll * (0.15f + 0.25f * (float)i) + par) % 400);
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

static void draw_quad(float x0, float y0, float x1, float y1,
                      float x2, float y2, float x3, float y3, u32 col)
{
	C2D_DrawTriangle(x0, y0, col, x1, y1, col, x2, y2, col, 0.3f);
	C2D_DrawTriangle(x0, y0, col, x2, y2, col, x3, y3, col, 0.3f);
}

static void draw_floor(ExoEye eye, ExoFlight *f)
{
	float ox, oy;
	int cx = f->cell_x;
	int cz = f->cell_z;
	int dx, dz, drawn = 0;
	const int R = EXO_FLIGHT_RENDER;

	exo_flight_eye_offset(f, exo_slider_3d(), (int)eye, &ox, &oy);
	(void)oy;

	for (dz = -2; dz <= R; ++dz) {
		for (dx = -R; dx <= R; ++dx) {
			int tx = cx + dx;
			int tz = cz + dz;
			float wx = (float)tx * EXO_FLIGHT_CELL;
			float wz = (float)tz * EXO_FLIGHT_CELL;
			float s[4], t[4];
			int ok = 0;
			uint16_t id;
			u32 col;

			if (exo_flight_project(f, wx, wz, ox, &s[0], &t[0])) ok++;
			if (exo_flight_project(f, wx + EXO_FLIGHT_CELL, wz, ox, &s[1], &t[1])) ok++;
			if (exo_flight_project(f, wx + EXO_FLIGHT_CELL, wz + EXO_FLIGHT_CELL, ox, &s[2], &t[2])) ok++;
			if (exo_flight_project(f, wx, wz + EXO_FLIGHT_CELL, ox, &s[3], &t[3])) ok++;
			if (ok < 4)
				continue;

			id = exo_tilemap_at(&f->map, tx, tz);
			col = tile_color(id);
			draw_quad(s[0], t[0], s[1], t[1], s[2], t[2], s[3], t[3], col);
			drawn++;
			if (drawn >= 256)
				goto done;
		}
	}
done:
	f->tiles_drawn = drawn;
}

static void draw_pilot(const ExoFlight *f)
{
	float x = 200.0f, y, w, h;
	int frame = 0;

	if (f->mode == EXO_FLIGHT_HIGH) {
		y = 148.0f; w = 36.0f; h = 22.0f; frame = 1;
	} else {
		y = 132.0f; w = 28.0f; h = 40.0f; frame = 0;
	}

	if (g_have_pilot) {
		size_t n = C2D_SpriteSheetCount(g_pilot);
		if (n == 0) return;
		if ((size_t)frame >= n) frame = 0;
		{
			C2D_Image img = C2D_SpriteSheetGetImage(g_pilot, frame);
			C2D_DrawImageAt(img, x - w * 0.5f, y, 0.6f, NULL, w / 32.0f, h / 32.0f);
		}
		return;
	}

	if (f->mode == EXO_FLIGHT_HIGH) {
		C2D_DrawTriangle(x, y, RGB32(240, 80, 70),
		                 x - 22.0f, y + 20.0f, RGB32(180, 40, 40),
		                 x + 22.0f, y + 20.0f, RGB32(180, 40, 40), 0.6f);
	} else {
		C2D_DrawRectSolid(x - 10.0f, y, 0.6f, 20.0f, 28.0f, RGB32(70, 180, 255));
		C2D_DrawRectSolid(x - 8.0f, y - 10.0f, 0.61f, 16.0f, 12.0f, RGB32(240, 200, 160));
	}
}

static void draw_hud(const ExoFlight *f)
{
	const ExoPilotStats *s = exo_pilot_stats(f->pilot);
	char line[64];
	float hud = exo_flight_hud_speed(f);
	float bar;

	exo_render_bottom(COL_BOT);
	exo_text_begin();
	snprintf(line, sizeof(line), "EMPIRE OF LUSTS");
	exo_text(8.0f, 8.0f, 0.55f, COL_ACCENT, line);
	snprintf(line, sizeof(line), "%s  %s", s->name,
	         f->mode == EXO_FLIGHT_HIGH ? "HIGH F-ZERO" : "LOW  3D");
	exo_text(8.0f, 32.0f, 0.5f, COL_TEXT, line);
	snprintf(line, sizeof(line), "SPD %3.0f %s", (double)hud, s->hud_unit);
	exo_text(8.0f, 56.0f, 0.5f, COL_TEXT, line);
	snprintf(line, sizeof(line), "REAL %5.1f / %5.1f", (double)f->speed, (double)s->vmax);
	exo_text(8.0f, 80.0f, 0.45f, COL_DIM, line);
	snprintf(line, sizeof(line), "TILE %03d %03d  DRAW %d", f->cell_x, f->cell_z, f->tiles_drawn);
	exo_text(8.0f, 104.0f, 0.45f, COL_DIM, line);
	snprintf(line, sizeof(line), "PAD move  L/R look  B brake");
	exo_text(8.0f, 160.0f, 0.4f, COL_DIM, line);
	snprintf(line, sizeof(line), "X sister   START pause");
	exo_text(8.0f, 180.0f, 0.4f, COL_DIM, line);
	if (g_paused)
		exo_text(8.0f, 208.0f, 0.5f, COL_ACCENT, "PAUSED");
	bar = f->speed / s->vmax;
	if (bar < 0.0f) bar = 0.0f;
	if (bar > 1.0f) bar = 1.0f;
	exo_bot_rect(8.0f, 132.0f, 304.0f, 10.0f, RGB32(30, 30, 40));
	exo_bot_rect(8.0f, 132.0f, 304.0f * bar, 10.0f,
	             f->mode == EXO_FLIGHT_HIGH ? RGB32(255, 90, 70)
	                                       : RGB32(80, 180, 255));
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
		if (exo_down(EXO_BTN_START))
			g_paused = !g_paused;
		if (exo_down(EXO_BTN_X)) {
			exo_flight_set_pilot(&g_flight,
				g_flight.pilot == EXO_PILOT_SHIRAMMY ? EXO_PILOT_REXXI : EXO_PILOT_SHIRAMMY);
		}
		if (!g_paused)
			exo_flight_tick(&g_flight, in, exo_dt());
		exo_render_begin();
		exo_render_eye(EXO_EYE_LEFT, COL_SKY);
		draw_layers(EXO_EYE_LEFT, &g_flight);
		draw_floor(EXO_EYE_LEFT, &g_flight);
		draw_pilot(&g_flight);
		exo_top_text(200.0f, 8.0f, 0.45f, COL_TEXT,
		             g_flight.mode == EXO_FLIGHT_HIGH ? "HIGH" : "LOW");
		if (exo_slider_3d() > 0.05f) {
			exo_render_eye(EXO_EYE_RIGHT, COL_SKY);
			draw_layers(EXO_EYE_RIGHT, &g_flight);
			draw_floor(EXO_EYE_RIGHT, &g_flight);
			draw_pilot(&g_flight);
			exo_top_text(200.0f, 8.0f, 0.45f, COL_TEXT,
			             g_flight.mode == EXO_FLIGHT_HIGH ? "HIGH" : "LOW");
		}
		draw_hud(&g_flight);
		exo_render_end();
		exo_frame_end();
	}
	if (g_pilot) C2D_SpriteSheetFree(g_pilot);
	if (g_bg[0]) C2D_SpriteSheetFree(g_bg[0]);
	if (g_bg[1]) C2D_SpriteSheetFree(g_bg[1]);
	if (g_bg[2]) C2D_SpriteSheetFree(g_bg[2]);
	exo_shutdown();
	return 0;
}

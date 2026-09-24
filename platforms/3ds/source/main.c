#include "exo/platform.h"
#include "exo/flight.h"
#include "exo/tilemap.h"

#include <3ds.h>
#include <citro2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
static C2D_SpriteSheet g_shi;
static C2D_SpriteSheet g_rex;
static bool g_have_bg[3];
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

static int emit_tile(ExoFlight *f, float ox, int tx, int tz, int *drawn, int cap)
{
	float sx[6], sy[6];
	int nv = 0;
	float pad = 0.7f;
	float wx, wz;

	if (*drawn >= cap)
		return 0;
	if (tx < 0 || tz < 0 || tx >= (int)f->map.w || tz >= (int)f->map.h)
		return 1;
	wx = (float)tx * EXO_FLIGHT_CELL - pad;
	wz = (float)tz * EXO_FLIGHT_CELL - pad;
	if (!exo_flight_clip_quad(f, wx, wz, EXO_FLIGHT_CELL + pad * 2.0f, ox, sx, sy, &nv))
		return 1;
	draw_poly(sx, sy, nv, tile_color(exo_tilemap_at(&f->map, tx, tz)));
	(*drawn)++;
	return 1;
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

	/* 1. anel perto — preenche debaixo dos pes, nunca fica de fora do cap */
	for (dz = -3; dz <= 8; ++dz)
		for (dx = -8; dx <= 8; ++dx)
			emit_tile(f, ox, cx + dx, cz + dz, &drawn, 400);

	/* 2. resto, longe → perto, com tecto */
	for (dz = R; dz >= 9; --dz)
		for (dx = -R; dx <= R; ++dx)
			if (!emit_tile(f, ox, cx + dx, cz + dz, &drawn, 280))
				goto done;

done:
	f->tiles_drawn = drawn;
}

static int pilot_frame(const ExoFlight *f)
{
	if (!f->grounded || f->y > 3.0f)
		return 3; /* flight */
	if (f->mode == EXO_FLIGHT_HIGH)
		return 2; /* high */
	if (f->speed > 2.5f)
		return 1; /* low / run */
	return 0; /* idle */
}

static void draw_pilot(const ExoFlight *f)
{
	C2D_SpriteSheet sheet;
	C2D_Image img;
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
		py = 200.0f;
	}
	py -= f->y * 1.35f;
	if (py > 236.0f) py = 236.0f;
	if (py < 40.0f) py = 40.0f;
	C2D_DrawImageAt(img, px - iw * 0.5f, py - ih, 0.62f, NULL, 1.0f, 1.0f);
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
	snprintf(line, sizeof(line), "A hold speed   B jump");
	exo_text(8.0f, 160.0f, 0.4f, COL_DIM, line);
	snprintf(line, sizeof(line), "PAD move  L/R look  X sister");
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
	if (g_shi) C2D_SpriteSheetFree(g_shi);
	if (g_rex) C2D_SpriteSheetFree(g_rex);
	if (g_bg[0]) C2D_SpriteSheetFree(g_bg[0]);
	if (g_bg[1]) C2D_SpriteSheetFree(g_bg[1]);
	if (g_bg[2]) C2D_SpriteSheetFree(g_bg[2]);
	exo_shutdown();
	return 0;
}

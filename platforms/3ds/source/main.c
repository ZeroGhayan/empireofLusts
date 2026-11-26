#include "exo/platform.h"
#include "exo/flight.h"
#include "exo/tilemap.h"
#include "exo/terrain.h"
#include "texfloor.h"
#include "exo/prof.h"

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
#define COL_TRUNK  RGB32(92, 58, 28)
#define COL_LEAF   RGB32(36, 130, 52)
#define COL_LEAF2  RGB32(22, 90, 38)
#define COL_BLUE   RGB32(80, 200, 255)
#define COL_RED    RGB32(255, 70, 70)
#define COL_PURPLE RGB32(170, 40, 210)

static const u32 TILE_COL[] = {
	RGB32( 46, 110,  58),
	RGB32( 28,  28,  32),
	RGB32( 58,  62,  74),
	RGB32(210, 200,  70),
	RGB32( 90,  86,  70),
	RGB32(150,  48, 190),
	RGB32(220,  36,  36),
	RGB32( 80, 160,  90)
};

static ExoFlight g_flight;
static C2D_SpriteSheet g_bg[3];
static C2D_SpriteSheet g_shi;
static C2D_SpriteSheet g_rex;
static C2D_SpriteSheet g_tiles;
static C2D_SpriteSheet g_tiles2;
static C2D_SpriteSheet g_cloud;
static int g_tile_n;
static int g_tile_n2;
static int g_tile_px = 32;
static int g_sno_packed;
static int g_vis_cx[720], g_vis_cz[720], g_vis_n;
static bool g_have_bg[3];
static bool g_paused;
static int g_menu = 1;
static int g_pick = 0;
static int g_pause_pick = 0;
static float g_fps = 60.0f;

/* --- neblina + distancia de desenho ---------------------------------- */
/* cor da neblina = cor do horizonte; ajustar junto com bg0..bg2 */
#define COL_FOG    RGB32(70, 96, 140)
static int   g_fog_on = 1;
static float g_fog_start, g_fog_end;   /* distancia horizontal em u */

/* --- perfilador (exo/prof.h) ----------------------------------------- */
enum { PH_INPUT, PH_TICK, PH_WAIT, PH_TOP_L, PH_TOP_R, PH_BOTTOM, PH_SUBMIT, PH_N };
static const char *const PH_NAMES[PH_N] = {
	"input", "tick", "wait", "top_l", "top_r", "bottom", "submit"
};
enum { CT_TILES, CT_CULLED, CT_VERTS, CT_BATCHES, CT_RENDER_R, CT_SLIDER, CT_N };
static const char *const CT_NAMES[CT_N] = {
	"tiles", "culled", "floor_verts", "floor_batches", "render_r", "slider_pct"
};
static int g_prof_mode;   /* 0 desligado, 1 HUD, 2 HUD + CSV no SD */

/* --- minimapa pre-renderizado ---------------------------------------- */
static int g_mini_dirty = 1;     /* refazer a textura no proximo HUD */

/* --- LOD distante: blocos de 4x4 celulas, cor media, alem do raio R ---- */
#define LOD_B      4
#define LOD_N      (EXO_TILEMAP_MAX / LOD_B)
#define LOD_FAR_K  3                  /* alcance = R * LOD_FAR_K celulas */
static u32     g_lod_col[LOD_N * LOD_N];
static int16_t g_lod_h[LOD_N * LOD_N];
static uint8_t g_lod_skip[LOD_N * LOD_N];
static int     g_lod_ready;
static int     g_lod_drawn;

/* --- menu de pausa ---------------------------------------------------- */
enum { PP_RESUME, PP_COURSES, PP_AERO, PP_FOG, PP_PROF, PP_N };

static u32 tile_color(uint16_t id)
{
	unsigned n = (unsigned)(sizeof(TILE_COL) / sizeof(TILE_COL[0]));
	return TILE_COL[id % n];
}

static void load_tileset(ExoCourse c)
{
	if (g_tiles) {
		C2D_SpriteSheetFree(g_tiles);
		g_tiles = NULL;
	}
	if (g_tiles2) {
		C2D_SpriteSheetFree(g_tiles2);
		g_tiles2 = NULL;
	}
	g_tile_n = 0;
	g_tile_n2 = 0;
	g_sno_packed = 0;
	g_tile_px = (c == EXO_COURSE_DP1 || c == EXO_COURSE_SNO) ? 8 : 32;
	if (c == EXO_COURSE_TEST) {
		g_tiles = C2D_SpriteSheetLoad("romfs:/gfx/test_tiles.t3x");
		g_tile_px = 32;
	} else if (c == EXO_COURSE_SS4)
		g_tiles = C2D_SpriteSheetLoad("romfs:/gfx/ss4_tiles.t3x");
	else if (c == EXO_COURSE_DP1)
		g_tiles = C2D_SpriteSheetLoad("romfs:/gfx/dp1_tiles.t3x");
	else if (c == EXO_COURSE_SNO) {
		g_tiles = C2D_SpriteSheetLoad("romfs:/gfx/sno_tiles.t3x");
		g_tiles2 = C2D_SpriteSheetLoad("romfs:/gfx/sno_tiles2.t3x");
	}
	if (g_tiles)
		g_tile_n = (int)C2D_SpriteSheetCount(g_tiles);
	if (g_tiles2)
		g_tile_n2 = (int)C2D_SpriteSheetCount(g_tiles2);
	if (c == EXO_COURSE_SNO && g_tile_n == 1)
		g_sno_packed = 1;
}

static int load_etm_file(const char *path, ExoCourse course)
{
	FILE *fp;
	uint8_t *buf;
	long sz;
	int ok = 0;

	fp = fopen(path, "rb");
	if (!fp)
		return exo_flight_load_map(&g_flight, course, NULL, 0), 0;
	fseek(fp, 0, SEEK_END);
	sz = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (sz <= 16 || sz > 128 * 128 * 2 + 64) {
		fclose(fp);
		return exo_flight_load_map(&g_flight, course, NULL, 0), 0;
	}
	buf = (uint8_t *)malloc((size_t)sz);
	if (!buf) {
		fclose(fp);
		return 0;
	}
	if (fread(buf, 1, (size_t)sz, fp) == (size_t)sz)
		ok = exo_flight_load_map(&g_flight, course, buf, (uint32_t)sz);
	free(buf);
	fclose(fp);
	return ok;
}

static void build_lod(const ExoFlight *f);

static void start_course(ExoCourse c)
{
	if (c == EXO_COURSE_TEST) {
		if (!load_etm_file("romfs:/flight/test/map.etm", EXO_COURSE_TEST))
			load_etm_file("romfs:/flight/sno/map.etm", EXO_COURSE_TEST);
	} else if (c == EXO_COURSE_SS4)
		load_etm_file("romfs:/flight/ss4/map.etm", EXO_COURSE_SS4);
	else if (c == EXO_COURSE_DP1)
		load_etm_file("romfs:/flight/dp1/map.etm", EXO_COURSE_DP1);
	else if (c == EXO_COURSE_SNO)
		load_etm_file("romfs:/flight/sno/map.etm", EXO_COURSE_SNO);
	else
		exo_flight_load_map(&g_flight, EXO_COURSE_DEMO, NULL, 0);
	load_tileset(c);
	g_paused = 0;
	g_pause_pick = 0;
	g_mini_dirty = 1;
	build_lod(&g_flight);
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
	g_cloud = C2D_SpriteSheetLoad("romfs:/gfx/clouds.t3x");
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
	if (n < 3) return;
	for (k = 1; k < n - 1; ++k)
		C2D_DrawTriangle(s[0], t[0], col, s[k], t[k], col, s[k + 1], t[k + 1], col, 0.34f);
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
	if (on == 0) return 0;
	if (sky == n) return 0;
	if (maxx < -40.0f || minx > 440.0f) return 0;
	if (maxy < -20.0f || miny > 250.0f) return 0;
	return 1;
}



static int tile_image(uint16_t id, C2D_Image *img)
{
	int i = (int)id;
	/* pool: um static sozinho reescrevia o UV de todos os quads do frame */
	static Tex3DS_SubTexture subpool[768];
	static int subi;

	if (g_sno_packed) {
		C2D_SpriteSheet sh;
		int local, col, row;
		const Tex3DS_SubTexture *base;
		Tex3DS_SubTexture *sub;

		if (i < 1024) {
			sh = g_tiles;
			local = i;
		} else {
			sh = g_tiles2;
			local = i - 1024;
		}
		if (!sh)
			return 0;
		*img = C2D_SpriteSheetGetImage(sh, 0);
		if (!img->subtex || !img->tex)
			return 0;
		base = img->subtex;
		col = local % 32;
		row = local / 32;
		if (subi >= 768)
			subi = 0;
		sub = &subpool[subi++];
		sub->width = 32;
		sub->height = 32;
		{
			float du = (base->right - base->left) / 32.0f;
			float dv = (base->bottom - base->top) / 32.0f;
			sub->left = base->left + du * (float)col;
			sub->right = base->left + du * (float)(col + 1);
			sub->top = base->top + dv * (float)row;
			sub->bottom = base->top + dv * (float)(row + 1);
		}
		img->subtex = sub;
		return 1;
	}
	if (g_tiles && g_tile_n > 0 && i < g_tile_n) {
		*img = C2D_SpriteSheetGetImage(g_tiles, i);
		return 1;
	}
	if (g_tiles2 && g_tile_n2 > 0 && i >= g_tile_n &&
	    i - g_tile_n < g_tile_n2) {
		*img = C2D_SpriteSheetGetImage(g_tiles2, i - g_tile_n);
		return 1;
	}
	if (g_tiles && g_tile_n > 0) {
		*img = C2D_SpriteSheetGetImage(g_tiles, i % g_tile_n);
		return 1;
	}
	return 0;
}

static float corner_h(const ExoFlight *f, int vx, int vz)
{
	float s = 0.0f;
	int n = 0, dx, dz;

	/* media so de pisos iguais: parede/bumper/agua nao entram, senao
	   a grama vira rampa colorida colada na caixa. */
	for (dz = -1; dz <= 0; ++dz) {
		for (dx = -1; dx <= 0; ++dx) {
			int cx = vx + dx;
			int cz = vz + dz;
			int16_t hh;
			ExoTerrain t;
			if (!exo_tilemap_walkable(&f->map, cx, cz))
				continue;
			hh = exo_tilemap_cell_h(&f->map, cx, cz);
			if (hh <= EXO_HEIGHT_VOID / 2)
				continue;
			t = exo_terrain_kind(f->course, &f->map,
			                     exo_tilemap_at(&f->map, cx, cz));
			if (t == EXO_TER_WALL || t == EXO_TER_BUMPER ||
			    t == EXO_TER_WATER)
				continue;
			s += (float)hh;
			n++;
		}
	}
	if (n <= 0)
		return 0.0f;
	return s / (float)n;
}

static int project_elev_quad(const ExoFlight *f, float wx, float wz, float ox,
                             const float *hy, float *qx, float *qy)
{
	static const float oxc[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
	static const float ozc[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
	int i, ok = 1;
	for (i = 0; i < 4; ++i) {
		if (!exo_flight_project3(f, wx + oxc[i] * EXO_FLIGHT_CELL, hy[i],
		                       wz + ozc[i] * EXO_FLIGHT_CELL, ox,
		                       &qx[i], &qy[i]))
			ok = 0;
	}
	return ok && quad_sane(qx, qy, 4, f->horizon);
}

static void emit_box(const ExoFlight *f, float wx, float wz, float ox,
                     float y0, float y1, u32 col, const C2D_Image *img)
{
	static const float oxc[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
	static const float ozc[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
	float topx[4], topy[4], botx[4], boty[4];
	int i, ok = 1;

	for (i = 0; i < 4; ++i) {
		if (!exo_flight_project3(f, wx + oxc[i] * EXO_FLIGHT_CELL, y1,
		                       wz + ozc[i] * EXO_FLIGHT_CELL, ox, &topx[i], &topy[i]))
			ok = 0;
		if (!exo_flight_project3(f, wx + oxc[i] * EXO_FLIGHT_CELL, y0,
		                       wz + ozc[i] * EXO_FLIGHT_CELL, ox, &botx[i], &boty[i]))
			ok = 0;
	}
	if (!ok)
		return;
	if (quad_sane(topx, topy, 4, f->horizon)) {
		if (img)
			exo_texfloor_quad(*img, topx, topy, 4);
		else
			draw_poly(topx, topy, 4, col);
	}
	for (i = 0; i < 4; ++i) {
		int j = (i + 1) & 3;
		float sx[4], sy[4];
		sx[0] = botx[i]; sy[0] = boty[i];
		sx[1] = botx[j]; sy[1] = boty[j];
		sx[2] = topx[j]; sy[2] = topy[j];
		sx[3] = topx[i]; sy[3] = topy[i];
		if (quad_sane(sx, sy, 4, f->horizon)) {
			if (img)
				exo_texfloor_quad(*img, sx, sy, 4);
			else
				draw_poly(sx, sy, 4, col);
		}
	}
}

static float fog_at(const ExoFlight *f, float wx, float wz)
{
	float dx, dz, d, t;
	if (!g_fog_on || g_fog_end <= g_fog_start)
		return 0.0f;
	dx = wx - f->cam_x;
	dz = wz - f->cam_z;
	d = sqrtf(dx * dx + dz * dz);
	t = (d - g_fog_start) / (g_fog_end - g_fog_start);
	if (t < 0.0f) return 0.0f;
	if (t > 1.0f) return 1.0f;
	return t * t * (3.0f - 2.0f * t);
}

static void draw_floor_fog_range(const ExoFlight *f);

/* fog dos 4 cantos de uma celula, mesma ordem de oxc/ozc */
static void fog_cell(const ExoFlight *f, int tx, int tz, float out[4])
{
	float wx = (float)tx * EXO_FLIGHT_CELL;
	float wz = (float)tz * EXO_FLIGHT_CELL;
	out[0] = fog_at(f, wx, wz);
	out[1] = fog_at(f, wx + EXO_FLIGHT_CELL, wz);
	out[2] = fog_at(f, wx + EXO_FLIGHT_CELL, wz + EXO_FLIGHT_CELL);
	out[3] = fog_at(f, wx, wz + EXO_FLIGHT_CELL);
}

static u32 fog_mix(u32 c, float t)
{
	u32 r, g, b, fr, fg, fb;
	if (t <= 0.0f)
		return c;
	if (t > 1.0f)
		t = 1.0f;
	r = c & 0xFF; g = (c >> 8) & 0xFF; b = (c >> 16) & 0xFF;
	fr = COL_FOG & 0xFF; fg = (COL_FOG >> 8) & 0xFF; fb = (COL_FOG >> 16) & 0xFF;
	r = (u32)((float)r + ((float)fr - (float)r) * t);
	g = (u32)((float)g + ((float)fg - (float)g) * t);
	b = (u32)((float)b + ((float)fb - (float)b) * t);
	return (c & 0xFF000000u) | r | (g << 8) | (b << 16);
}

static int g_pass; /* 0 nuvem  1 piso  2 caixa */

static int emit_tile(ExoFlight *f, float ox, int tx, int tz,
                     int *drawn, int *culled, int cap, float far_z)
{
	float sx[6], sy[6];
	int nv = 0;
	float wx, wz;
	float fg[4], fog_c;
	uint16_t id;

	/* limites do mapa e visibilidade ja foram testados em gather_cells() */
	(void)far_z;
	if (g_pass != 0 && *drawn >= cap)
		return 0;
	if (tx < 0 || tz < 0 || tx >= (int)f->map.w || tz >= (int)f->map.h) {
		(*culled)++;
		return 1;
	}
	wx = (float)tx * EXO_FLIGHT_CELL;
	wz = (float)tz * EXO_FLIGHT_CELL;
	id = exo_tilemap_at(&f->map, tx, tz);
	fog_cell(f, tx, tz, fg);
	fog_c = (fg[0] + fg[1] + fg[2] + fg[3]) * 0.25f;
	exo_texfloor_fog4(fg);
	{
		ExoTerrain ter = exo_terrain_kind(f->course, &f->map, id);
		float hy[4];
		C2D_Image img;
		int have_img;

		/* pass 0: camada de nuvem sob TODA a pista SNO (id 0 e deck) */
		if (g_pass == 0) {
			if (f->course == EXO_COURSE_SNO) {
				float ch[4] = { EXO_SNO_CLOUD_Y, EXO_SNO_CLOUD_Y,
				                EXO_SNO_CLOUD_Y, EXO_SNO_CLOUD_Y };
				float cqx[4], cqy[4];
				uint16_t cid = (uint16_t)(((tx % 21) + 21) % 21);
				if (project_elev_quad(f, wx, wz, ox, ch, cqx, cqy)) {
					if (tile_image(cid, &img))
						exo_texfloor_quad(img, cqx, cqy, 4);
					else if (g_cloud && C2D_SpriteSheetCount(g_cloud) > 0) {
						img = C2D_SpriteSheetGetImage(g_cloud, 0);
						exo_texfloor_quad(img, cqx, cqy, 4);
					} else {
						draw_poly(cqx, cqy, 4, fog_mix(RGB32(210, 230, 245), fog_c));
					}
				}
			}
			return 1;
		}

		if (f->course == EXO_COURSE_SNO && id == 0)
			return 1;

		if (f->course == EXO_COURSE_TEST) {
			if (ter == EXO_TER_WATER) {
				hy[0] = hy[1] = hy[2] = hy[3] = 0.0f;
			} else {
				exo_flight_cell_hy(f, tx, tz, hy);
			}
		} else if (f->course == EXO_COURSE_SNO) {
			hy[0] = corner_h(f, tx, tz);
			hy[1] = corner_h(f, tx + 1, tz);
			hy[2] = corner_h(f, tx + 1, tz + 1);
			hy[3] = corner_h(f, tx, tz + 1);
		} else if (f->course == EXO_COURSE_DP1) {
			hy[0] = corner_h(f, tx, tz);
			hy[1] = corner_h(f, tx + 1, tz);
			hy[2] = corner_h(f, tx + 1, tz + 1);
			hy[3] = corner_h(f, tx, tz + 1);
		} else {
			hy[0] = hy[1] = hy[2] = hy[3] = 0.0f;
		}

		/* caixas de parede DP1 (antes passe 2; agora no mesmo laco de
		   tras para frente do piso). bumper SS4 e piso plano. */
		if (f->course == EXO_COURSE_DP1 && ter == EXO_TER_WALL) {
			exo_texfloor_fog1(fog_c);
			{
				C2D_Image boximg;
				int have_box = tile_image(id, &boximg);
				emit_box(f, wx, wz, ox, 0.0f, EXO_FLIGHT_WALL_H,
				         fog_mix(RGB32(90, 70, 50), fog_c),
				         have_box ? &boximg : NULL);
			}
			if (g_vis_n < 720) {
				g_vis_cx[g_vis_n] = tx;
				g_vis_cz[g_vis_n] = tz;
				g_vis_n++;
			}
			(*drawn)++;
			return 1;
		}

		if (ter == EXO_TER_WALL)
			return 1;

		/* bumper Sonic CD: mesma altura do chao, sprite no plano */

		have_img = tile_image(id, &img);
		if (have_img) {
			float qx[4], qy[4];
			if (f->course == EXO_COURSE_TEST &&
			    exo_flight_cell_dual(f, tx, tz)) {
				float lo[4] = {
					EXO_TEST_DUAL_LO, EXO_TEST_DUAL_LO,
					EXO_TEST_DUAL_LO, EXO_TEST_DUAL_LO
				};
				if (project_elev_quad(f, wx, wz, ox, lo, qx, qy))
					exo_texfloor_quad(img, qx, qy, 4);
			}
			if (f->course == EXO_COURSE_TEST &&
			    exo_flight_cell_exp(f, tx, tz)) {
				int s;
				exo_texfloor_fog1(fog_c);
				{
					float floorh[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
					if (project_elev_quad(f, wx, wz, ox, floorh, qx, qy))
						exo_texfloor_quad(img, qx, qy, 4);
				}
				for (s = 0; s < 4; ++s) {
					float z0 = (float)s * 0.25f;
					float z1 = z0 + 0.25f;
					float hs[4], qxs[4], qys[4];
					float wz0 = wz + z0 * EXO_FLIGHT_CELL;
					int iok = 1, k;
					hs[0] = exo_flight_surf_h(f, wx, wz0);
					hs[1] = exo_flight_surf_h(f, wx + EXO_FLIGHT_CELL, wz0);
					hs[2] = exo_flight_surf_h(f, wx + EXO_FLIGHT_CELL,
					                           wz + z1 * EXO_FLIGHT_CELL);
					hs[3] = exo_flight_surf_h(f, wx,
					                           wz + z1 * EXO_FLIGHT_CELL);
					for (k = 0; k < 4; ++k) {
						static const float oxc[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
						static const float ozc[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
						float lz = (k == 0 || k == 1) ? z0 : z1;
						(void)ozc;
						if (!exo_flight_project3(f,
						        wx + oxc[k] * EXO_FLIGHT_CELL, hs[k],
						        wz + lz * EXO_FLIGHT_CELL, ox,
						        &qxs[k], &qys[k]))
							iok = 0;
					}
					if (iok && quad_sane(qxs, qys, 4, f->horizon))
						exo_texfloor_quad(img, qxs, qys, 4);
				}
				if (g_vis_n < 720) {
					g_vis_cx[g_vis_n] = tx;
					g_vis_cz[g_vis_n] = tz;
					g_vis_n++;
				}
				(*drawn)++;
				return 1;
			}
			if (project_elev_quad(f, wx, wz, ox, hy, qx, qy)) {
				exo_texfloor_quad(img, qx, qy, 4);
				if (g_vis_n < 720) {
					g_vis_cx[g_vis_n] = tx;
					g_vis_cz[g_vis_n] = tz;
					g_vis_n++;
				}
				(*drawn)++;
				return 1;
			}
			(*culled)++;
			return 1;
		}

		if (f->course == EXO_COURSE_TEST && ter == EXO_TER_WATER) {
			float wh[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			float qx[4], qy[4];
			if (project_elev_quad(f, wx, wz, ox, wh, qx, qy))
				draw_poly(qx, qy, 4, fog_mix(RGB32(0, 40, 220), fog_c));
			if (g_vis_n < 720) {
				g_vis_cx[g_vis_n] = tx;
				g_vis_cz[g_vis_n] = tz;
				g_vis_n++;
			}
			(*drawn)++;
			return 1;
		}
	}

	if (!exo_flight_clip_quad(f, wx, wz, EXO_FLIGHT_CELL, ox, sx, sy, &nv)) {
		(*culled)++;
		return 1;
	}
	if (!quad_sane(sx, sy, nv, f->horizon)) {
		(*culled)++;
		return 1;
	}
	draw_poly(sx, sy, nv, fog_mix(tile_color(id), fog_c));
	if (g_vis_n < 720) {
		g_vis_cx[g_vis_n] = tx;
		g_vis_cz[g_vis_n] = tz;
		g_vis_n++;
	}
	(*drawn)++;
	return 1;
}

static int cell_drawn(int cx, int cz)
{
	int i;
	for (i = 0; i < g_vis_n; ++i)
		if (g_vis_cx[i] == cx && g_vis_cz[i] == cz)
			return 1;
	return 0;
}

static void draw_goal_mark(const ExoFlight *f, float ox)
{
	static const float oxc[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
	static const float ozc[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
	float wx, wz, qx[4], qy[4];
	int i, ok = 1;

	if (!exo_flight_is_goal(f, f->goal_cx, f->goal_cz))
		return;
	wx = (float)f->goal_cx * EXO_FLIGHT_CELL;
	wz = (float)f->goal_cz * EXO_FLIGHT_CELL;
	for (i = 0; i < 4; ++i) {
		if (!exo_flight_project3(f, wx + oxc[i] * EXO_FLIGHT_CELL, 0.4f,
		                       wz + ozc[i] * EXO_FLIGHT_CELL, ox,
		                       &qx[i], &qy[i]))
			ok = 0;
	}
	if (ok && quad_sane(qx, qy, 4, f->horizon))
		draw_poly(qx, qy, 4, COL_PURPLE);
}

/* celulas visiveis do olho atual, em ordem de anel (perto -> longe) */
#define VIS_MAX ((2 * EXO_FLIGHT_RENDER_MAX + 1) * (2 * EXO_FLIGHT_RENDER_MAX + 1))
static short g_cand_x[VIS_MAX], g_cand_z[VIS_MAX];
static int g_cand_n, g_cand_cap_n;

static void gather_cells(ExoFlight *f, float ox, int R, int cap, float far_z,
                         int *culled)
{
	int ccx, ccz, ring, dx, dz, counted = 0;

	g_cand_n = 0;
	g_cand_cap_n = 0;
	ccx = (int)floorf(f->cam_x / EXO_FLIGHT_CELL);
	ccz = (int)floorf(f->cam_z / EXO_FLIGHT_CELL);
	for (ring = 0; ring <= R; ++ring) {
		for (dz = -ring; dz <= ring; ++dz) {
			int step = (dz == -ring || dz == ring) ? 1 : 2 * ring;
			for (dx = -ring; dx <= ring; dx += (step > 0 ? step : 1)) {
				int tx = ccx + dx, tz = ccz + dz;
				float lz;
				if (tx < 0 || tz < 0 || tx >= (int)f->map.w ||
				    tz >= (int)f->map.h) {
					(*culled)++;
					continue;
				}
				if (!exo_flight_tile_visible(f, (float)tx * EXO_FLIGHT_CELL,
				                             (float)tz * EXO_FLIGHT_CELL,
				                             EXO_FLIGHT_CELL, ox, far_z, &lz)) {
					(*culled)++;
					continue;
				}
				if (g_cand_n >= VIS_MAX)
					return;
				g_cand_x[g_cand_n] = (short)tx;
				g_cand_z[g_cand_n] = (short)tz;
				g_cand_n++;
				/* o limite (cap) vale so para o piso, como antes; ceu
				   vazio do SNO (id 0) nao conta */
				if (!(f->course == EXO_COURSE_SNO &&
				      exo_tilemap_at(&f->map, tx, tz) == 0))
					counted++;
				if (counted <= cap)
					g_cand_cap_n = g_cand_n;
			}
		}
	}
}

/*
 * Ordem de desenho: antes os aneis iam de perto para longe e, como o piso
 * nao usa depth buffer, morros/rampas distantes eram pintados POR CIMA do
 * chao proximo (o "glitch" que piora com a distancia de render). Agora a
 * lista visivel e montada de perto para longe (para o limite de tiles
 * cortar so os mais distantes) e desenhada ao contrario (pintor).
 * Passe 1 (piso) e 2 (caixas de parede DP1) foram juntados no mesmo laco
 * para que caixas longe nao cubram piso perto.
 */
static void draw_floor(ExoEye eye, ExoFlight *f)
{
	float ox, oy;
	int i, drawn = 0, culled = 0;
	int R = f->render_r;
	int cap = f->draw_cap;
	float far_z;

	g_vis_n = 0;
	if (R < EXO_FLIGHT_RENDER_MIN) R = EXO_FLIGHT_RENDER_MIN;
	if (R > EXO_FLIGHT_RENDER_MAX) R = EXO_FLIGHT_RENDER_MAX;
	if (cap < 40) cap = 40;

	far_z = (float)R * EXO_FLIGHT_CELL + 8.0f;
	draw_floor_fog_range(f);
	exo_texfloor_fog_enable(g_fog_on);
	exo_texfloor_fog_color(COL_FOG);
	exo_flight_eye_offset(f, exo_slider_3d(), (int)eye, &ox, &oy);
	(void)oy;

	gather_cells(f, ox, R, cap, far_z, &culled);

	/* 0 nuvem (SNO, sem limite)  1 piso + caixa de parede */
	for (g_pass = 0; g_pass <= 1; ++g_pass) {
		int n = (g_pass == 0) ? g_cand_n : g_cand_cap_n;
		if (g_pass == 0 && f->course != EXO_COURSE_SNO)
			continue;
		exo_texfloor_begin();
		for (i = n - 1; i >= 0; --i)
			emit_tile(f, ox, g_cand_x[i], g_cand_z[i], &drawn, &culled,
			          1 << 30, far_z);
		exo_texfloor_end();
	}
	culled += g_cand_n - g_cand_cap_n;
	draw_goal_mark(f, ox);
	f->tiles_drawn = drawn;
	f->tiles_culled = culled;
}

static void draw_floor_fog_range(const ExoFlight *f)
{
	int R = f->render_r;
	if (R < EXO_FLIGHT_RENDER_MIN) R = EXO_FLIGHT_RENDER_MIN;
	if (R > EXO_FLIGHT_RENDER_MAX) R = EXO_FLIGHT_RENDER_MAX;
	if (g_lod_ready) {
		g_fog_start = (float)R * EXO_FLIGHT_CELL * 0.8f;
		g_fog_end = (float)(R * LOD_FAR_K) * EXO_FLIGHT_CELL;
	} else {
		g_fog_end = (float)R * EXO_FLIGHT_CELL;
		g_fog_start = g_fog_end * 0.55f;
	}
}

static void draw_trees(const ExoFlight *f)
{
	int i;

	for (i = 0; i < f->tree_n; ++i) {
		float wx, wz, bx, by, tx, ty, tw;
		if (!cell_drawn(f->trees[i].cx, f->trees[i].cz) &&
		    !cell_drawn(f->trees[i].cx + 1, f->trees[i].cz) &&
		    !cell_drawn(f->trees[i].cx, f->trees[i].cz + 1))
			continue;
		wx = ((float)f->trees[i].cx + f->trees[i].ox) * EXO_FLIGHT_CELL;
		wz = ((float)f->trees[i].cz + f->trees[i].oz) * EXO_FLIGHT_CELL;
		{
			float gh = exo_tilemap_sample_h(&f->map, wx, wz);
			if (gh < (float)EXO_HEIGHT_VOID / 2.0f)
				gh = 0.0f;
			if (!exo_flight_project3(f, wx, gh, wz, 0.0f, &bx, &by))
				continue;
			if (by < f->horizon - 2.0f)
				continue;
			if (!exo_flight_project3(f, wx, gh + f->trees[i].h, wz, 0.0f, &tx, &ty))
				continue;
		}
		if (by < ty) {
			float tmp = by; by = ty; ty = tmp;
		}
		tw = (by - ty) * 0.28f;
		if (tw < 3.0f) tw = 3.0f;
		if (bx < -40.0f || bx > 440.0f || by < -20.0f || ty > 250.0f)
			continue;
		C2D_DrawRectSolid(bx - tw * 0.18f, ty + (by - ty) * 0.45f, 0.38f,
		                  tw * 0.36f, (by - ty) * 0.55f, COL_TRUNK);
		C2D_DrawTriangle(bx, ty, COL_LEAF,
		                 bx - tw, ty + (by - ty) * 0.62f, COL_LEAF2,
		                 bx + tw, ty + (by - ty) * 0.62f, COL_LEAF, 0.39f);
	}
}

static int pilot_frame(const ExoFlight *f)
{
	if (f->y > 2.5f || f->submerged) return 3;
	if (f->mode == EXO_FLIGHT_HIGH) return 2;
	if (f->speed > 2.5f) return 1;
	return 0;
}

static void draw_pilot(const ExoFlight *f)
{
	C2D_SpriteSheet sheet;
	C2D_Image img;
	C2D_DrawParams par;
	float px, py, iw, ih;
	int frame = pilot_frame(f);
	size_t n;

	sheet = (f->pilot == EXO_PILOT_REXXI) ? g_rex : g_shi;
	if (!sheet) return;
	n = C2D_SpriteSheetCount(sheet);
	if (n == 0) return;
	if ((size_t)frame >= n) frame = 0;
	img = C2D_SpriteSheetGetImage(sheet, frame);
	if (!img.subtex) return;
	iw = img.subtex->width;
	ih = img.subtex->height;

	if (!exo_flight_project(f, f->x, f->z, 0.0f, &px, &py)) {
		px = 200.0f;
		py = EXO_FLIGHT_PIN_FLY;
	}
	if (py > 228.0f) py = 228.0f;
	if (py < 36.0f) py = 36.0f;

	par.pos.x = px; par.pos.y = py; par.pos.w = iw; par.pos.h = ih;
	par.center.x = iw * 0.5f; par.center.y = ih;
	par.depth = 0.44f;
	par.angle = f->slope_roll +
	            ((f->cam_ref == EXO_CAM_SURFACE) ? (f->bank * EXO_FLIGHT_CAM_ROLL +
	                                                f->pitch * 0.25f) : f->bank * 0.20f);
	C2D_DrawImage(img, &par, NULL);
}

static void draw_speed_veil(const ExoFlight *f)
{
	float frac, a;
	u32 col;

	if (f->pilot != EXO_PILOT_SHIRAMMY) return;
	frac = exo_flight_speed_frac(f);
	if (frac < 0.75f) return;
	a = (frac - 0.75f) / 0.25f;
	if (a > 1.0f) a = 1.0f;
	a = a * a;
	col = ((u32)(130.0f * a) << 24);
	C2D_DrawRectSolid(0.0f, 0.0f, 0.46f, 400.0f, 240.0f, col);
}

static void draw_charge_bar(const ExoFlight *f)
{
	float w = 72.0f * f->charge;
	u32 fill = f->charge_armed ? COL_RED : COL_BLUE;

	C2D_DrawRectSolid(8.0f, 224.0f, 0.62f, 74.0f, 8.0f, RGB32(20, 22, 32));
	if (w > 0.5f)
		C2D_DrawRectSolid(9.0f, 225.0f, 0.63f, w, 6.0f, fill);
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
	if (f->run == EXO_RUN_DONE)
		snprintf(line, sizeof(line), "CLEAR");
	else if (f->course == EXO_COURSE_DP1)
		snprintf(line, sizeof(line), "LAP %d/3", f->laps);
	else if (f->course == EXO_COURSE_SS4)
		snprintf(line, sizeof(line), "PAD %d/3", f->score);
	else if (f->course == EXO_COURSE_SNO)
		snprintf(line, sizeof(line), "LAP %d/3", f->laps);
	else if (f->course == EXO_COURSE_TEST)
		snprintf(line, sizeof(line), "Y %.0f", (double)f->y);
	else
		snprintf(line, sizeof(line), "T %.2f", (double)f->run_t);
	exo_top_text(200.0f, 220.0f, 0.42f, COL_TEXT, line);
	if (f->run == EXO_RUN_DONE)
		exo_top_text(200.0f, 110.0f, 0.55f, COL_ACCENT, "COURSE CLEAR");
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
	exo_bot_line(cx - arm, cy, cx + arm, cy, RGB32(50, 54, 70));
	exo_bot_line(cx, cy - arm, cx, cy + arm, RGB32(50, 54, 70));
	exo_bot_line(cx, cy, cx + hx, cy + hz, COL_HEAD);
	exo_bot_line(cx, cy, cx + mx, cy + mz, COL_SLIDE);
	exo_bot_line(cx, cy, cx + pdx, cy + pdy, COL_PAD);
	exo_text(x0, y0 + s + 2.0f, 0.28f, COL_PAD, "PAD");
	exo_text(x0 + 28.0f, y0 + s + 2.0f, 0.28f, COL_HEAD, "YAW");
	exo_text(x0 + 56.0f, y0 + s + 2.0f, 0.28f, COL_SLIDE, "MOV");
}

static void draw_slider(const char *label, float t, int selected,
                        float x, float y, float w)
{
	u32 bar = selected ? COL_ACCENT : RGB32(40, 44, 58);
	u32 fill = selected ? COL_SLIDE : RGB32(80, 100, 130);
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;
	exo_text(x, y, 0.28f, selected ? COL_ACCENT : COL_DIM, label);
	exo_bot_rect(x, y + 10.0f, w, 6.0f, bar);
	exo_bot_rect(x, y + 10.0f, w * t, 6.0f, fill);
}

static int touch_in(const ExoInput *in, float x, float y, float w, float h)
{
	if (!in || !in->touch_held) return 0;
	return (float)in->touch_x >= x && (float)in->touch_x <= x + w &&
	       (float)in->touch_y >= y && (float)in->touch_y <= y + h;
}

static void apply_tune(ExoFlight *f, const ExoInput *in)
{
	float rend_t;
	const float sx = 8.0f, sw = 196.0f;

	if (g_paused)
		return;
	if (in && in->touch_press &&
	    (float)in->touch_x >= sx && (float)in->touch_x <= sx + sw &&
	    (float)in->touch_y >= 128.0f && (float)in->touch_y <= 148.0f)
		f->cam_ref = (f->cam_ref == EXO_CAM_SURFACE) ? EXO_CAM_PILOT : EXO_CAM_SURFACE;
	if (touch_in(in, sx, 208.0f, sw, 28.0f)) {
		rend_t = ((float)in->touch_x - sx) / sw;
		if (rend_t < 0.0f) rend_t = 0.0f;
		if (rend_t > 1.0f) rend_t = 1.0f;
		f->render_r = EXO_FLIGHT_RENDER_MIN +
		              (int)(rend_t * (float)(EXO_FLIGHT_RENDER_MAX - EXO_FLIGHT_RENDER_MIN) + 0.5f);
	}
	if (exo_down(EXO_BTN_SELECT))
		exo_flight_reset_run(f);
}

static void draw_menu(void)
{
	exo_render_bottom(COL_BOT);
	exo_text_begin();
	exo_text(8.0f, 8.0f, 0.32f, COL_ACCENT, "EMPIRE OF LUSTS");
	exo_text(8.0f, 24.0f, 0.28f, COL_TEXT, "SELECT COURSE");
	exo_text(8.0f, 48.0f, 0.28f, COL_ACCENT, "  TEST  FISICA / CAMERA");
	exo_text(8.0f, 72.0f, 0.26f, COL_DIM, "RAMPAS U  EXP  GELO");
	exo_text(8.0f, 86.0f, 0.26f, COL_DIM, "AGUA  DUPLA  MOLA");
	exo_text(8.0f, 110.0f, 0.28f, COL_TEXT, "A START (TEST)");
	exo_text(8.0f, 96.0f + 40.0f, 0.28f, COL_PAD, "Y VOO LIVRE (plano aberto)");
	exo_text(8.0f, 154.0f, 0.28f, COL_DIM, "START  QUIT");
	exo_text(8.0f, 172.0f, 0.24f, COL_DIM, "SELECT RESET  B PULO  ZL PERFIL");
}

static void draw_pause(void)
{
	static const char *const prof_txt[3] = { "OFF", "HUD", "HUD + CSV" };
	char line[48];
	int i;
	exo_bot_rect(40.0f, 30.0f, 240.0f, 150.0f, RGB32(16, 16, 28));
	exo_text(52.0f, 38.0f, 0.34f, COL_ACCENT, "PAUSED");
	for (i = 0; i < PP_N; ++i) {
		u32 c = (g_pause_pick == i) ? COL_ACCENT : COL_TEXT;
		switch (i) {
		case PP_RESUME:  snprintf(line, sizeof(line), "  RESUME"); break;
		case PP_COURSES: snprintf(line, sizeof(line), "  COURSE SELECT"); break;
		case PP_AERO:    snprintf(line, sizeof(line), "  VOO: %s",
		                          g_flight.aero_on ? "AVIAO (novo)" : "ARCADE (antigo)"); break;
		case PP_FOG:     snprintf(line, sizeof(line), "  NEBLINA + LOD: %s",
		                          g_fog_on ? "ON" : "OFF"); break;
		default:         snprintf(line, sizeof(line), "  PERFIL: %s",
		                          prof_txt[g_prof_mode]); break;
		}
		exo_text(52.0f, 60.0f + 18.0f * (float)i, 0.30f, c, line);
	}
	exo_text(52.0f, 156.0f, 0.24f, COL_DIM, "CIMA/BAIXO  A ESCOLHE  B VOLTA");
}

static void prof_set_mode(int m)
{
	if (m < 0 || m > 2)
		m = 0;
	if (m == 2) {
		if (!exo_prof_log_start())
			m = 1; /* sem SD/pasta: fica so no HUD */
	} else {
		exo_prof_log_stop();
	}
	g_prof_mode = m;
}

/* painel do perfilador: media e pico (1 s) por fase, em ms */
static void draw_prof_panel(void)
{
	const ExoProfStats *st = exo_prof_stats();
	const float x0 = 110.0f, y0 = 104.0f;
	char line[64];
	int i, n = exo_prof_phase_count();
	float busy = st->frame_avg_ms - st->avg_ms[PH_WAIT];

	exo_bot_rect(x0, y0, 206.0f, 102.0f, RGB32(10, 10, 16));
	snprintf(line, sizeof(line), "QUADRO %5.2f ms  PICO %5.2f  CPU %5.2f",
	         (double)st->frame_avg_ms, (double)st->frame_peak_ms, (double)busy);
	exo_text(x0 + 2.0f, y0 + 1.0f, 0.24f,
	         busy > 16.6f ? COL_RED : COL_SLIDE, line);
	for (i = 0; i < n; ++i) {
		float w = st->avg_ms[i] * 4.0f;
		if (w > 120.0f) w = 120.0f;
		snprintf(line, sizeof(line), "%-6s %5.2f %5.2f", exo_prof_phase_name(i),
		         (double)st->avg_ms[i], (double)st->peak_ms[i]);
		exo_text(x0 + 2.0f, y0 + 11.0f + 9.0f * (float)i, 0.22f, COL_TEXT, line);
		exo_bot_rect(x0 + 84.0f, y0 + 14.0f + 9.0f * (float)i, w, 4.0f,
		             i == PH_WAIT ? RGB32(60, 70, 90) : COL_ACCENT);
	}
	snprintf(line, sizeof(line), "GPU proc %.2f draw %.2f  VTX %d LOTES %d",
	         (double)st->gpu_proc_ms, (double)st->gpu_draw_ms,
	         exo_texfloor_vertices(), exo_texfloor_batches());
	exo_text(x0 + 2.0f, y0 + 76.0f, 0.22f, COL_DIM, line);
	if (st->logging)
		snprintf(line, sizeof(line), "CSV sdmc:/3ds/eol/prof_%03d.csv  %lu",
		         st->log_index, (unsigned long)st->rows_written);
	else
		snprintf(line, sizeof(line), "CSV desligado (pausa > PERFIL)");
	exo_text(x0 + 2.0f, y0 + 86.0f, 0.22f, st->logging ? COL_RED : COL_DIM, line);
}

static void draw_flight_diag(const ExoFlight *f)
{
	const float x0 = 110.0f, y0 = 132.0f;
	float pfrac, needle;
	const char *land, *trend;
	u32 land_c, trend_c;
	char line[32];

	pfrac = f->pitch / EXO_FLIGHT_PITCH_MAX;
	if (pfrac > 1.0f) pfrac = 1.0f;
	if (pfrac < -1.0f) pfrac = -1.0f;
	needle = pfrac * 36.0f;

	exo_bot_rect(x0, y0, 96.0f, 68.0f, RGB32(18, 20, 30));
	exo_text(x0 + 2.0f, y0 + 2.0f, 0.26f, COL_ACCENT, "PITCH");
	exo_bot_line(x0 + 48.0f, y0 + 28.0f, x0 + 48.0f + needle, y0 + 16.0f,
	             COL_HEAD);
	exo_bot_line(x0 + 12.0f, y0 + 28.0f, x0 + 84.0f, y0 + 28.0f,
	             RGB32(50, 54, 70));
	snprintf(line, sizeof(line), "%+.0f",
	         (double)(((f->mode == EXO_FLIGHT_HIGH) ? f->body_pitch : f->pitch)
	                  * (180.0f / 3.14159265f)));
	exo_text(x0 + 52.0f, y0 + 2.0f, 0.26f, COL_TEXT, line);
	snprintf(line, sizeof(line), "R%+.0f",
	         (double)(f->body_roll * (180.0f / 3.14159265f)));
	exo_text(x0 + 52.0f, y0 + 16.0f, 0.22f, COL_DIM, line);

	if (f->submerged) {
		land = "WATER";
		land_c = COL_BLUE;
	} else if (f->grounded &&
	           exo_terrain_kind(f->course, &f->map,
	                            exo_tilemap_at(&f->map, f->cell_x, f->cell_z))
	           == EXO_TER_ICE) {
		land = "ICE";
		land_c = COL_PAD;
	} else if (f->grounded) {
		land = "LAND";
		land_c = COL_SLIDE;
	} else {
		land = "AIR";
		land_c = COL_PAD;
	}
	exo_text(x0 + 2.0f, y0 + 34.0f, 0.26f, land_c, land);

	if (f->spd_delta > 1.2f) {
		trend = "SPD UP";
		trend_c = COL_SLIDE;
	} else if (f->spd_delta < -1.2f) {
		trend = "SPD DOWN";
		trend_c = COL_RED;
	} else {
		trend = "SPD HOLD";
		trend_c = COL_DIM;
	}
	exo_text(x0 + 2.0f, y0 + 48.0f, 0.26f, trend_c, trend);
}

static u32 minimap_color(const ExoFlight *f, int cx, int cz)
{
	uint16_t id = exo_tilemap_at(&f->map, cx, cz);
	ExoTerrain ter = exo_terrain_kind(f->course, &f->map, id);
	if (id == 0 && f->course == EXO_COURSE_SNO)
		return RGB32(28, 70, 120);
	if (f->course == EXO_COURSE_TEST && id == 0)
		return RGB32(40, 140, 60);
	if (ter == EXO_TER_WATER)  return RGB32(40, 80, 170);
	if (ter == EXO_TER_WALL)   return RGB32(40, 40, 48);
	if (ter == EXO_TER_BUMPER) return RGB32(200, 50, 50);
	if (ter == EXO_TER_FINISH) return RGB32(230, 210, 60);
	if (ter == EXO_TER_SPRING) return RGB32(230, 180, 40);
	if (ter == EXO_TER_ICE)    return RGB32(140, 230, 240);
	if (ter == EXO_TER_DUAL)   return RGB32(220, 80, 200);
	if (ter == EXO_TER_BOOST || ter == EXO_TER_BOOST_N ||
	    ter == EXO_TER_BOOST_S || ter == EXO_TER_BOOST_E ||
	    ter == EXO_TER_BOOST_W)
		return RGB32(180, 50, 190);
	if (f->course == EXO_COURSE_SNO)
		return RGB32(90, 160, 120);
	return RGB32(70, 130, 70);
}

/*
 * LOD: o mapa e resumido em blocos 4x4 (cor media do minimapa + altura
 * media do piso) 1x por curso. Alem do raio fino R o horizonte e
 * preenchido por esses blocos (1 quad por bloco), coberto de neblina; o
 * resultado e ver longe no voo sem pagar 16 tiles por bloco.
 */
static void build_lod(const ExoFlight *f)
{
	int bx, bz, x, z, pw = (int)f->map.w, ph = (int)f->map.h;
	g_lod_ready = 0;
	if (!f->map.loaded || pw <= 0 || ph <= 0)
		return;
	for (bz = 0; bz < LOD_N; ++bz) {
		for (bx = 0; bx < LOD_N; ++bx) {
			u32 r = 0, g = 0, b = 0;
			int n = 0, nh = 0, voids = 0;
			long hs = 0;
			for (z = bz * LOD_B; z < bz * LOD_B + LOD_B; ++z) {
				for (x = bx * LOD_B; x < bx * LOD_B + LOD_B; ++x) {
					u32 c;
					int16_t hh;
					if (x >= pw || z >= ph)
						continue;
					if (f->course == EXO_COURSE_SNO &&
					    exo_tilemap_at(&f->map, x, z) == 0) {
						voids++;
						continue;
					}
					c = minimap_color(f, x, z);
					r += c & 0xFF; g += (c >> 8) & 0xFF; b += (c >> 16) & 0xFF;
					n++;
					hh = exo_tilemap_cell_h(&f->map, x, z);
					if (hh > EXO_HEIGHT_VOID / 2) {
						hs += hh;
						nh++;
					}
				}
			}
			g_lod_skip[bz * LOD_N + bx] = (n == 0 || voids > n);
			if (n > 0)
				g_lod_col[bz * LOD_N + bx] = RGB32(r / (u32)n, g / (u32)n, b / (u32)n);
			g_lod_h[bz * LOD_N + bx] = (int16_t)(nh > 0 ? hs / nh : 0);
		}
	}
	g_lod_ready = 1;
}

static void draw_poly_z(const float *s, const float *t, int n, u32 col, float z)
{
	int k;
	if (n < 3) return;
	for (k = 1; k < n - 1; ++k)
		C2D_DrawTriangle(s[0], t[0], col, s[k], t[k], col, s[k + 1], t[k + 1], col, z);
}

/*
 * "Saia" de neblina: um quad da cor da neblina do horizonte (linha do
 * infinito do chao) para baixo, atras do terreno. Tapa a faixa de ceu
 * vazio que sobrava entre o fim do alcance e o horizonte.
 */
static void draw_fog_skirt(const ExoFlight *f)
{
	float qx[4], qy[4];
	float yinf;
	int i;
	if (!g_fog_on)
		return;
	yinf = f->horizon + EXO_FLIGHT_FOCAL * tanf(f->pitch) - 1.0f;
	qx[0] = -400.0f; qy[0] = yinf;
	qx[1] =  800.0f; qy[1] = yinf;
	qx[2] =  800.0f; qy[2] = yinf + 900.0f;
	qx[3] = -400.0f; qy[3] = yinf + 900.0f;
	for (i = 0; i < 4; ++i)
		exo_flight_apply_ref(f, &qx[i], &qy[i]);
	draw_poly_z(qx, qy, 4, COL_FOG, 0.25f);
}

/* desenhado ANTES do piso fino: o piso pinta por cima onde houver tile */
static void draw_lod(ExoEye eye, ExoFlight *f)
{
	static const float oxc[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
	static const float ozc[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
	const float B = EXO_FLIGHT_CELL * (float)LOD_B;
	float ox, oy, far_z;
	int R = f->render_r, Rf, ccx, ccz, cbx, cbz, rb, ring, dx, dz;

	g_lod_drawn = 0;
	if (!g_fog_on || !g_lod_ready)
		return;
	if (R < EXO_FLIGHT_RENDER_MIN) R = EXO_FLIGHT_RENDER_MIN;
	if (R > EXO_FLIGHT_RENDER_MAX) R = EXO_FLIGHT_RENDER_MAX;
	Rf = R * LOD_FAR_K;
	far_z = (float)Rf * EXO_FLIGHT_CELL;
	exo_flight_eye_offset(f, exo_slider_3d(), (int)eye, &ox, &oy);
	(void)oy;
	ccx = (int)floorf(f->cam_x / EXO_FLIGHT_CELL);
	ccz = (int)floorf(f->cam_z / EXO_FLIGHT_CELL);
	cbx = ccx / LOD_B;
	cbz = ccz / LOD_B;
	rb = Rf / LOD_B + 1;
	for (ring = rb; ring >= 0; --ring) {
		for (dz = -ring; dz <= ring; ++dz) {
			int step = (dz == -ring || dz == ring) ? 1 : 2 * ring;
			for (dx = -ring; dx <= ring; dx += (step > 0 ? step : 1)) {
				int bx = cbx + dx, bz = cbz + dz, i, ok = 1;
				int x0, z0, x1, z1;
				float wx, wz, qx[4], qy[4], fg, h, lz;
				if (bx < 0 || bz < 0 || bx >= LOD_N || bz >= LOD_N)
					continue;
				if (g_lod_skip[bz * LOD_N + bx])
					continue;
				/* blocos perto so sao pulados quando o piso fino cobre de
				   certeza (raio R/2): o limite de tiles (draw_cap) pode
				   cortar o anel fino antes de R e deixava buracos. */
				x0 = bx * LOD_B - ccx; x1 = x0 + LOD_B - 1;
				z0 = bz * LOD_B - ccz; z1 = z0 + LOD_B - 1;
				if (x0 >= -R / 2 && x1 <= R / 2 && z0 >= -R / 2 && z1 <= R / 2)
					continue;
				wx = (float)bx * B;
				wz = (float)bz * B;
				if (!exo_flight_tile_visible(f, wx, wz, B, ox, far_z, &lz))
					continue;
				h = (float)g_lod_h[bz * LOD_N + bx];
				for (i = 0; i < 4 && ok; ++i)
					ok = exo_flight_project3(f, wx + oxc[i] * B, h,
					                         wz + ozc[i] * B, ox, &qx[i], &qy[i]);
				if (!ok || !quad_sane(qx, qy, 4, f->horizon))
					continue;
				fg = fog_at(f, wx + B * 0.5f, wz + B * 0.5f);
				draw_poly_z(qx, qy, 4, fog_mix(g_lod_col[bz * LOD_N + bx], fg), 0.33f);
				g_lod_drawn++;
			}
		}
	}
}

/*
 * Minimapa pre-renderizado: antes eram 96x96 = 9216 retangulos do citro2d
 * POR QUADRO, mais que o limite de 8192 objetos do C2D_Init -> o resto do
 * HUD sumia/piscava (glitch da tela de baixo) e o custo de CPU era alto.
 * Agora a textura 128x128 (1 pixel por celula) e montada 1x ao carregar o
 * curso e desenhada como uma unica imagem; so marcador e destino mudam.
 */
static void build_minimap(const ExoFlight *f)
{
	int cx, cz, pw = (int)f->map.w, ph = (int)f->map.h;
	exo_minimap_begin(pw, ph);
	for (cz = 0; cz < ph && cz < 128; ++cz)
		for (cx = 0; cx < pw && cx < 128; ++cx)
			/* +X do mundo = direita do minimapa (espelhado como antes) */
			exo_minimap_put(pw - 1 - cx, cz, minimap_color(f, cx, cz));
	exo_minimap_end();
	g_mini_dirty = 0;
}

static void draw_minimap(const ExoFlight *f)
{
	const int x0 = 8, y0 = 108, s = 96;
	int pw, ph;

	if (!f->map.loaded || f->map.w == 0 || f->map.h == 0)
		return;
	pw = (int)f->map.w;
	ph = (int)f->map.h;
	if (g_mini_dirty)
		build_minimap(f);
	exo_bot_rect((float)x0 - 1, (float)y0 - 1, (float)s + 2, (float)s + 2,
	             RGB32(16, 18, 28));
	exo_minimap_draw((float)x0, (float)y0, (float)s);
	/* destino (marcador fixo, sem piloto automatico) */
	if (f->goal_cx >= 0 && f->goal_cz >= 0) {
		float gx = (float)x0 + (float)(s - 1) - ((float)f->goal_cx + 0.5f) * (float)s / (float)pw;
		float gz = (float)y0 + ((float)f->goal_cz + 0.5f) * (float)s / (float)ph;
		exo_bot_line(gx - 4.0f, gz, gx + 4.0f, gz, COL_PURPLE);
		exo_bot_line(gx, gz - 4.0f, gx, gz + 4.0f, COL_PURPLE);
		exo_bot_line(gx - 1.0f, gz, gx + 1.0f, gz, COL_TEXT);
	}
	{
		float px = (float)x0 + (float)(s - 1) - (f->x / EXO_FLIGHT_CELL) * (float)s / (float)pw;
		float pz = (float)y0 + (f->z / EXO_FLIGHT_CELL) * (float)s / (float)ph;
		/* rumo: x espelhado no minimapa, z para baixo */
		float hx = -sinf(f->yaw) * 7.0f;
		float hz = cosf(f->yaw) * 7.0f;
		exo_bot_line(px, pz, px + hx, pz + hz, RGB32(255, 240, 80));
		exo_bot_line(px - 2.0f, pz, px + 2.0f, pz, RGB32(255, 240, 80));
		exo_bot_line(px, pz - 2.0f, px, pz + 2.0f, RGB32(255, 240, 80));
	}
}

static void draw_hud(const ExoFlight *f)
{
	const ExoPilotStats *s = exo_pilot_stats(f->pilot);
	char line[64];
	float hud = exo_flight_hud_speed(f);
	float hud_max = exo_flight_hud_max(f);
	float bar;
	float rend_t = (float)(f->render_r - EXO_FLIGHT_RENDER_MIN) /
	               (float)(EXO_FLIGHT_RENDER_MAX - EXO_FLIGHT_RENDER_MIN);
	int tx, ty;
	uint16_t id = exo_tilemap_at(&f->map, f->cell_x, f->cell_z);

	exo_terrain_xy(f->course, id, &tx, &ty);

	exo_render_bottom(COL_BOT);
	exo_text_begin();
	exo_text(8.0f, 4.0f, 0.32f, COL_ACCENT, "EMPIRE OF LUSTS");
	snprintf(line, sizeof(line), "%s %s", s->name,
	         f->mode == EXO_FLIGHT_HIGH ? "HIGH" : "LOW");
	exo_text(8.0f, 16.0f, 0.28f, COL_TEXT, line);
	if (f->pilot == EXO_PILOT_REXXI)
		snprintf(line, sizeof(line), "SPD %4.2f %s", (double)hud, s->hud_unit);
	else
		snprintf(line, sizeof(line), "SPD %4.0f %s", (double)hud, s->hud_unit);
	exo_text(8.0f, 28.0f, 0.28f, COL_TEXT, line);
	snprintf(line, sizeof(line), "ID [%d,%d] W %.1f", tx, ty, (double)f->water_t);
	exo_text(8.0f, 40.0f, 0.28f, f->submerged ? COL_BLUE : COL_DIM, line);
	snprintf(line, sizeof(line), "TILE %03d %03d TS %d PX %d",
	         f->cell_x, f->cell_z, g_tile_n, g_tile_px);
	exo_text(8.0f, 52.0f, 0.28f, COL_DIM, line);
	snprintf(line, sizeof(line), "DRAW %d/%d", f->tiles_drawn, f->draw_cap);
	exo_text(8.0f, 64.0f, 0.28f, COL_DIM, line);

	bar = hud_max > 0.0f ? hud / hud_max : 0.0f;
	if (bar < 0.0f) bar = 0.0f;
	if (bar > 1.0f) bar = 1.0f;
	exo_bot_rect(8.0f, 76.0f, 196.0f, 6.0f, RGB32(30, 30, 40));
	exo_bot_rect(8.0f, 76.0f, 196.0f * bar, 6.0f,
	             f->charge_armed ? COL_RED : COL_BLUE);

	exo_text(8.0f, 86.0f, 0.28f, COL_ACCENT, "CAM");
	exo_text(40.0f, 86.0f, 0.28f, COL_TEXT,
	         f->cam_ref == EXO_CAM_SURFACE ? "SURFACE" : "PILOT");

	if (f->course == EXO_COURSE_DP1)
		snprintf(line, sizeof(line), "LAP %d/3 T %.2f", f->laps, (double)f->run_t);
	else if (f->course == EXO_COURSE_SS4)
		snprintf(line, sizeof(line), "PAD %d/3 T %.2f", f->score, (double)f->run_t);
	else if (f->course == EXO_COURSE_SNO)
		snprintf(line, sizeof(line), "LAP %d/3 T %.2f", f->laps, (double)f->run_t);
	else if (f->course == EXO_COURSE_TEST)
		snprintf(line, sizeof(line), "TEST T %.2f H %.1f", (double)f->run_t, (double)f->y);
	else
		snprintf(line, sizeof(line), "T %.2f BEST %.2f", (double)f->run_t, (double)f->best_t);
	exo_text(8.0f, 98.0f, 0.28f, COL_ACCENT, line);

	draw_minimap(f);
	if (g_prof_mode == 0)
		draw_flight_diag(f);
	{
		char rr[24];
		snprintf(rr, sizeof(rr), "RENDER R %d", f->render_r);
		draw_slider(rr, rend_t, 1, 8.0f, 208.0f, 196.0f);
	}
	draw_pad_graph(f);

	if (g_prof_mode == 0) {
		if (f->aero_active)
			exo_text(110.0f, 108.0f, 0.26f, COL_DIM,
			         f->aero.stalling ? "ESTOL: NARIZ DESCE  A ASAS" : "A ASAS  B FREIO  L/R LEME");
		else
			exo_text(110.0f, 108.0f, 0.26f, COL_DIM,
			         f->mode == EXO_FLIGHT_HIGH ? "Y SPEED A BRAKE B TOFF" : "PAD LOOK Y SPEED");
		exo_text(110.0f, 120.0f, 0.26f, COL_DIM, "ZR CAM  SELECT RESET  ZL PERFIL");
	} else {
		draw_prof_panel();
	}
	if (g_paused)
		draw_pause();
}

static void draw_world(ExoEye eye)
{
	exo_flight_cam_update(&g_flight);
	exo_render_eye(eye, COL_SKY);
	draw_layers(eye, &g_flight);
	/* draw_floor calcula a faixa de neblina; draw_lod usa a do quadro */
	draw_floor_fog_range(&g_flight);
	draw_fog_skirt(&g_flight);
	draw_lod(eye, &g_flight);
	draw_floor(eye, &g_flight);
	draw_trees(&g_flight);
	draw_pilot(&g_flight);
	draw_speed_veil(&g_flight);
	draw_charge_bar(&g_flight);
	draw_top_overlay(&g_flight);
}

static void prof_tag_frame(const ExoInput *in)
{
	char tag[16];
	static const char cn[] = "DS4NT";
	int c = (int)g_flight.course;
	snprintf(tag, sizeof(tag), "%c%c%c%c%c",
	         (c >= 0 && c < 5) ? cn[c] : '?',
	         g_flight.pilot == EXO_PILOT_REXXI ? 'R' : 'S',
	         g_flight.aero_active ? 'A' : (g_flight.mode == EXO_FLIGHT_HIGH ? 'H' : 'L'),
	         g_fog_on ? 'F' : 'f',
	         (in && in->new_3ds) ? 'N' : 'o');
	exo_prof_tag(tag);
}

int main(void)
{
	if (!exo_init())
		return 1;
	exo_prof_init("eol", PH_NAMES, PH_N, CT_NAMES, CT_N);
	exo_flight_init(&g_flight, EXO_PILOT_SHIRAMMY);
	load_gfx();
	exo_texfloor_init();
	for (;;) {
		const ExoInput *in;
		float dt;
		int alive;

		exo_prof_frame_begin();
		exo_prof_begin(PH_INPUT);
		alive = exo_frame_begin();
		exo_prof_end(PH_INPUT);
		if (!alive)
			break;
		in = exo_input();
		dt = exo_dt();

		g_fps = g_fps * 0.90f + (dt > 0.0001f ? (1.0f / dt) : 60.0f) * 0.10f;

		exo_prof_begin(PH_TICK);
		if (exo_down(EXO_BTN_ZL))
			prof_set_mode((g_prof_mode + 1) % 3);
		if (g_menu) {
			if (exo_down(EXO_BTN_START))
				break;
			if (exo_down(EXO_BTN_A)) {
				start_course(EXO_COURSE_TEST);
				g_menu = 0;
			} else if (exo_down(EXO_BTN_Y)) {
				/* plano aberto 128x128 com destino marcado: o unico mapa
				   atual onde da para voar sem cair no vazio */
				start_course(EXO_COURSE_DEMO);
				g_menu = 0;
			}
		} else {
			if (exo_down(EXO_BTN_START)) {
				g_paused = !g_paused;
				g_pause_pick = 0;
			}
			if (g_paused) {
				if (exo_down(EXO_BTN_UP))
					g_pause_pick = (g_pause_pick + PP_N - 1) % PP_N;
				if (exo_down(EXO_BTN_DOWN))
					g_pause_pick = (g_pause_pick + 1) % PP_N;
				if (exo_down(EXO_BTN_B)) {
					g_paused = 0;
				} else if (exo_down(EXO_BTN_A)) {
					switch (g_pause_pick) {
					case PP_RESUME:
						g_paused = 0;
						break;
					case PP_COURSES:
						g_menu = 1;
						g_paused = 0;
						break;
					case PP_AERO:
						g_flight.aero_on = !g_flight.aero_on;
						g_flight.aero_active = 0;
						break;
					case PP_FOG:
						g_fog_on = !g_fog_on;
						break;
					default:
						prof_set_mode((g_prof_mode + 1) % 3);
						break;
					}
				}
			} else {
				if (exo_down(EXO_BTN_X))
					exo_flight_set_pilot(&g_flight,
						g_flight.pilot == EXO_PILOT_SHIRAMMY ? EXO_PILOT_REXXI : EXO_PILOT_SHIRAMMY);
				apply_tune(&g_flight, in);
				exo_flight_tick(&g_flight, in, dt);
				if (g_flight.run == EXO_RUN_DONE &&
				    g_flight.done_t >= 5.0f) {
					g_menu = 1;
					g_paused = 0;
				}
			}
		}
		exo_prof_end(PH_TICK);

		/* C3D_FrameBegin espera VBlank + GPU do quadro anterior: tempo
		   ocioso. CPU real = quadro - wait. */
		exo_prof_begin(PH_WAIT);
		exo_render_begin();
		exo_prof_end(PH_WAIT);
		exo_texfloor_frame();
		if (!g_menu) {
			exo_prof_begin(PH_TOP_L);
			draw_world(EXO_EYE_LEFT);
			exo_prof_end(PH_TOP_L);
			if (exo_slider_3d() > 0.05f) {
				exo_prof_begin(PH_TOP_R);
				draw_world(EXO_EYE_RIGHT);
				exo_prof_end(PH_TOP_R);
			}
			exo_prof_begin(PH_BOTTOM);
			draw_hud(&g_flight);
			exo_prof_end(PH_BOTTOM);
		} else {
			exo_render_eye(EXO_EYE_LEFT, COL_SKY);
			exo_prof_begin(PH_BOTTOM);
			draw_menu();
			exo_prof_end(PH_BOTTOM);
		}
		exo_prof_begin(PH_SUBMIT);
		exo_render_end();
		exo_frame_end();
		exo_prof_end(PH_SUBMIT);

		exo_prof_counter(CT_TILES, g_flight.tiles_drawn);
		exo_prof_counter(CT_CULLED, g_flight.tiles_culled);
		exo_prof_counter(CT_VERTS, exo_texfloor_vertices());
		exo_prof_counter(CT_BATCHES, exo_texfloor_batches());
		exo_prof_counter(CT_RENDER_R, g_flight.render_r);
		exo_prof_counter(CT_SLIDER, (int)(exo_slider_3d() * 100.0f + 0.5f));
		exo_prof_gpu(C3D_GetProcessingTime(), C3D_GetDrawingTime());
		prof_tag_frame(in);
		exo_prof_frame_end();
	}
	exo_prof_shutdown();
	exo_texfloor_fini();
	if (g_tiles) C2D_SpriteSheetFree(g_tiles);
	if (g_tiles2) C2D_SpriteSheetFree(g_tiles2);
	if (g_cloud) C2D_SpriteSheetFree(g_cloud);
	if (g_shi) C2D_SpriteSheetFree(g_shi);
	if (g_rex) C2D_SpriteSheetFree(g_rex);
	if (g_bg[0]) C2D_SpriteSheetFree(g_bg[0]);
	if (g_bg[1]) C2D_SpriteSheetFree(g_bg[1]);
	if (g_bg[2]) C2D_SpriteSheetFree(g_bg[2]);
	exo_shutdown();
	return 0;
}

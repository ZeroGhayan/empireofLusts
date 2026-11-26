#include "exo/platform.h"
#include "exo/flight.h"
#include "exo/tilemap.h"
#include "exo/terrain.h"
#include "texfloor.h"

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

static int g_pass; /* 0 nuvem  1 piso  2 caixa */

static int emit_tile(ExoFlight *f, float ox, int tx, int tz,
                     int *drawn, int *culled, int cap, float far_z)
{
	float sx[6], sy[6];
	int nv = 0;
	float wx, wz, lz;
	uint16_t id;

	if (g_pass != 0 && *drawn >= cap)
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
	id = exo_tilemap_at(&f->map, tx, tz);
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
						draw_poly(cqx, cqy, 4, RGB32(210, 230, 245));
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

		/* pass 2: so caixas de parede DP1. bumper SS4 e piso plano. */
		if (g_pass == 2) {
			if (ter != EXO_TER_WALL)
				return 1;
			{
				C2D_Image boximg;
				int have_box = tile_image(id, &boximg);
				emit_box(f, wx, wz, ox, 0.0f, EXO_FLIGHT_WALL_H,
				         RGB32(90, 70, 50),
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
				draw_poly(qx, qy, 4, RGB32(0, 40, 220));
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
	draw_poly(sx, sy, nv, tile_color(id));
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

static void draw_floor(ExoEye eye, ExoFlight *f)
{
	float ox, oy;
	int ccx, ccz, ring, dx, dz, drawn = 0, culled = 0;
	int R = f->render_r;
	int cap = f->draw_cap;
	float far_z;

	g_vis_n = 0;
	if (R < EXO_FLIGHT_RENDER_MIN) R = EXO_FLIGHT_RENDER_MIN;
	if (R > EXO_FLIGHT_RENDER_MAX) R = EXO_FLIGHT_RENDER_MAX;
	if (cap < 40) cap = 40;

	ccx = (int)floorf(f->cam_x / EXO_FLIGHT_CELL);
	ccz = (int)floorf(f->cam_z / EXO_FLIGHT_CELL);
	far_z = (float)R * EXO_FLIGHT_CELL + 8.0f;
	exo_flight_eye_offset(f, exo_slider_3d(), (int)eye, &ox, &oy);
	(void)oy;

	/* 0 nuvem (SNO, nao conta no cap)  1 piso  2 parede-caixa */
	for (g_pass = 0; g_pass <= 2; ++g_pass) {
		if (g_pass == 0 && f->course != EXO_COURSE_SNO)
			continue;
		if (g_pass == 2 && f->course != EXO_COURSE_DP1)
			continue;
		exo_texfloor_begin();
		for (ring = 0; ring <= R; ++ring) {
			int stop = 0;
			for (dz = -ring; dz <= ring && !stop; ++dz) {
				for (dx = -ring; dx <= ring; ++dx) {
					int ax = dx < 0 ? -dx : dx;
					int az = dz < 0 ? -dz : dz;
					int cheb = ax > az ? ax : az;
					if (cheb != ring)
						continue;
					if (!emit_tile(f, ox, ccx + dx, ccz + dz,
					               &drawn, &culled, cap, far_z)) {
						stop = 1;
						break;
					}
				}
			}
			if (stop)
				break;
		}
		exo_texfloor_end();
	}
	draw_goal_mark(f, ox);
	f->tiles_drawn = drawn;
	f->tiles_culled = culled;
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
	exo_text(8.0f, 110.0f, 0.28f, COL_TEXT, "A START");
	exo_text(8.0f, 128.0f, 0.28f, COL_DIM, "START  QUIT");
	exo_text(8.0f, 150.0f, 0.24f, COL_DIM, "SELECT RESET  B PULO");
}

static void draw_pause(void)
{
	exo_bot_rect(40.0f, 40.0f, 240.0f, 120.0f, RGB32(16, 16, 28));
	exo_text(52.0f, 50.0f, 0.34f, COL_ACCENT, "PAUSED");
	exo_text(52.0f, 78.0f, 0.30f, g_pause_pick == 0 ? COL_ACCENT : COL_TEXT, "  RESUME");
	exo_text(52.0f, 96.0f, 0.30f, g_pause_pick == 1 ? COL_ACCENT : COL_TEXT, "  COURSE SELECT");
	exo_text(52.0f, 128.0f, 0.26f, COL_DIM, "UP/DOWN  A CONFIRM");
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

static void draw_minimap(const ExoFlight *f)
{
	const int x0 = 8, y0 = 108, s = 96;
	int mx, mz;
	int pw, ph;

	if (!f->map.loaded || f->map.w == 0 || f->map.h == 0)
		return;
	pw = (int)f->map.w;
	ph = (int)f->map.h;
	exo_bot_rect((float)x0 - 1, (float)y0 - 1, (float)s + 2, (float)s + 2,
	             RGB32(16, 18, 28));
	for (mz = 0; mz < s; ++mz) {
		int cz = mz * ph / s;
		for (mx = 0; mx < s; ++mx) {
			int cx = mx * pw / s;
			uint16_t id = exo_tilemap_at(&f->map, cx, cz);
			ExoTerrain ter = exo_terrain_kind(f->course, &f->map, id);
			u32 col;
			if (id == 0 && f->course == EXO_COURSE_SNO)
				col = RGB32(28, 70, 120);
			else if (f->course == EXO_COURSE_TEST && id == 0)
				col = RGB32(40, 140, 60);
			else if (ter == EXO_TER_WATER)
				col = RGB32(40, 80, 170);
			else if (ter == EXO_TER_WALL)
				col = RGB32(40, 40, 48);
			else if (ter == EXO_TER_BUMPER)
				col = RGB32(200, 50, 50);
			else if (ter == EXO_TER_FINISH)
				col = RGB32(230, 210, 60);
			else if (ter == EXO_TER_SPRING)
				col = RGB32(230, 180, 40);
			else if (ter == EXO_TER_ICE)
				col = RGB32(140, 230, 240);
			else if (ter == EXO_TER_DUAL)
				col = RGB32(220, 80, 200);
			else if (ter == EXO_TER_BOOST || ter == EXO_TER_BOOST_N ||
			         ter == EXO_TER_BOOST_S || ter == EXO_TER_BOOST_E ||
			         ter == EXO_TER_BOOST_W)
				col = RGB32(180, 50, 190);
			else if (f->course == EXO_COURSE_SNO)
				col = RGB32(90, 160, 120);
			else
				col = RGB32(70, 130, 70);
			/* +X do mundo = direita do minimapa (antes vinha espelhado) */
			exo_bot_dot(x0 + (s - 1 - mx), y0 + mz, col);
		}
	}
	{
		int px = x0 + (s - 1) - f->cell_x * s / pw;
		int pz = y0 + f->cell_z * s / ph;
		exo_bot_rect((float)(px - 1), (float)(pz - 1), 3, 3, RGB32(255, 240, 80));
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
	draw_flight_diag(f);
	{
		char rr[24];
		snprintf(rr, sizeof(rr), "RENDER R %d", f->render_r);
		draw_slider(rr, rend_t, 1, 8.0f, 208.0f, 196.0f);
	}
	draw_pad_graph(f);

	exo_text(110.0f, 108.0f, 0.26f, COL_DIM,
	         f->mode == EXO_FLIGHT_HIGH ? "Y SPEED A BRAKE B TOFF" : "PAD LOOK Y SPEED");
	exo_text(110.0f, 120.0f, 0.26f, COL_DIM, "ZR CAM  SELECT RESET");
	if (g_paused)
		draw_pause();
}

static void draw_world(ExoEye eye)
{
	exo_render_eye(eye, COL_SKY);
	draw_layers(eye, &g_flight);
	draw_floor(eye, &g_flight);
	draw_trees(&g_flight);
	draw_pilot(&g_flight);
	draw_speed_veil(&g_flight);
	draw_charge_bar(&g_flight);
	draw_top_overlay(&g_flight);
}

int main(void)
{
	if (!exo_init())
		return 1;
	exo_flight_init(&g_flight, EXO_PILOT_SHIRAMMY);
	load_gfx();
	exo_texfloor_init();
	while (exo_frame_begin()) {
		const ExoInput *in = exo_input();
		float dt = exo_dt();

		g_fps = g_fps * 0.90f + (dt > 0.0001f ? (1.0f / dt) : 60.0f) * 0.10f;

		if (g_menu) {
			if (exo_down(EXO_BTN_START))
				break;
			if (exo_down(EXO_BTN_A)) {
				start_course(EXO_COURSE_TEST);
				g_menu = 0;
			}
		} else {
			if (exo_down(EXO_BTN_START)) {
				g_paused = !g_paused;
				g_pause_pick = 0;
			}
			if (g_paused) {
				if (exo_down(EXO_BTN_UP) || exo_down(EXO_BTN_DOWN))
					g_pause_pick = 1 - g_pause_pick;
				if (exo_down(EXO_BTN_A) || exo_down(EXO_BTN_B)) {
					if (g_pause_pick == 1 && exo_down(EXO_BTN_A)) {
						g_menu = 1;
						g_paused = 0;
					} else {
						g_paused = 0;
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

		exo_render_begin();
		if (!g_menu) {
			draw_world(EXO_EYE_LEFT);
			if (exo_slider_3d() > 0.05f)
				draw_world(EXO_EYE_RIGHT);
			draw_hud(&g_flight);
		} else {
			exo_render_eye(EXO_EYE_LEFT, COL_SKY);
			draw_menu();
		}
		exo_render_end();
		exo_frame_end();
	}
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

#include "exo/flight.h"
#include "exo/terrain.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static const ExoPilotStats STATS[2] = {
	{
		.vmax = 380.0f, .walk = 40.0f, .run = 92.0f, .accel = 70.0f, .coast = 24.0f,
		.turn_low = 2.2f, .turn_high = 1.65f, .jump = 36.0f, .climb = 58.0f,
		.hud_max = EXO_FLIGHT_HUD_SHI, .hud_unit = "Gm/h", .name = "Shirammy"
	},
	{
		.vmax = 200.0f, .walk = 28.0f, .run = 84.0f, .accel = 32.0f, .coast = 36.0f,
		.turn_low = 2.5f, .turn_high = 2.15f, .jump = 40.0f, .climb = 50.0f,
		.hud_max = EXO_FLIGHT_HUD_REX, .hud_unit = "Mm/h", .name = "Rexxi"
	}
};

const ExoPilotStats *exo_pilot_stats(ExoPilot p)
{
	if ((unsigned)p > (unsigned)EXO_PILOT_REXXI)
		p = EXO_PILOT_SHIRAMMY;
	return &STATS[p];
}

static float ground_h(const ExoFlight *f, float wx, float wz);

static float clampf(float v, float lo, float hi)
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

float exo_flight_vmax(const ExoFlight *f)
{
	return exo_pilot_stats(f->pilot)->vmax;
}

static ExoTerrain cell_ter(const ExoFlight *f, int cx, int cz)
{
	return exo_terrain_kind(f->course, &f->map, exo_tilemap_at(&f->map, cx, cz));
}

/* TEST 6x5: shape 0 flat  1 slope  2 exp  3 dual  4 water  5 ice  6 spring */
enum {
	TS_FLAT = 0, TS_SLOPE, TS_EXP, TS_DUAL, TS_WATER, TS_ICE, TS_SPRING
};

typedef struct TestSpec {
	uint8_t shape;
	int16_t href;
	int8_t  deg;
} TestSpec;

static const TestSpec TEST_SPEC[30] = {
	/* row 0 */
	{ TS_FLAT,  30,   0 }, { TS_WATER,  0,  0 }, { TS_FLAT,  10,  0 },
	{ TS_EXP,   70,   0 }, { TS_EXP,   30,  0 }, { TS_SLOPE,  0, 40 },
	/* row 1 */
	{ TS_DUAL,  10,   0 }, { TS_FLAT,  70,  0 }, { TS_FLAT,  60,  0 },
	{ TS_FLAT,  50,   0 }, { TS_FLAT,  40,  0 }, { TS_FLAT,  20,  0 },
	/* row 2 */
	{ TS_ICE,   10,   0 }, { TS_FLAT,   0,  0 }, { TS_EXP,   60,  0 },
	{ TS_SLOPE,  0,  30 }, { TS_EXP,   50,  0 }, { TS_SLOPE,  0, 20 },
	/* row 3 */
	{ TS_EXP,   40,   0 }, { TS_SLOPE,  0, 10 }, { TS_FLAT,  10,  0 },
	{ TS_EXP,   20,   0 }, { TS_SLOPE,  0,-10 }, { TS_EXP,   10,  0 },
	/* row 4 */
	{ TS_SLOPE,  0, -20 }, { TS_EXP,    0,  0 }, { TS_SLOPE,  0,-30 },
	{ TS_EXP,    0,   0 }, { TS_SLOPE,  0,-40 }, { TS_SPRING, 0,  0 }
};

static const TestSpec *test_spec_id(uint16_t id)
{
	if (id >= 30)
		return &TEST_SPEC[0];
	return &TEST_SPEC[id];
}

static float testdeg_rise(int deg)
{
	float a = (float)deg * 0.01745329252f;
	float t = tanf(a < 0.0f ? -a : a);
	return EXO_FLIGHT_CELL * t;
}

static float test_exp01(float t)
{
	float k = EXO_TEST_EXP_K;
	if (t <= 0.0f)
		return 0.0f;
	if (t >= 1.0f)
		return 1.0f;
	return (expf(k * t) - 1.0f) / (expf(k) - 1.0f);
}

static uint16_t test_id(const ExoFlight *f, int cx, int cz)
{
	return exo_tilemap_at(&f->map, cx, cz);
}

int exo_flight_cell_exp(const ExoFlight *f, int cx, int cz)
{
	if (!f || f->course != EXO_COURSE_TEST)
		return 0;
	return test_spec_id(test_id(f, cx, cz))->shape == TS_EXP;
}

int exo_flight_cell_dual(const ExoFlight *f, int cx, int cz)
{
	if (!f || f->course != EXO_COURSE_TEST)
		return 0;
	return test_spec_id(test_id(f, cx, cz))->shape == TS_DUAL;
}

static float test_delta(const TestSpec *sp)
{
	if (sp->shape == TS_SLOPE) {
		float r = testdeg_rise(sp->deg);
		return (sp->deg >= 0) ? r : -r;
	}
	if (sp->shape == TS_EXP)
		return (float)sp->href;
	return 0.0f;
}

/* height[] no TEST = aresta SUL da celula (liga rampas em U). */
static float test_south(const ExoTilemap *m, int cx, int cz)
{
	if (!exo_tilemap_walkable(m, cx, cz))
		return 0.0f;
	return (float)exo_tilemap_cell_h(m, cx, cz);
}

static float test_sample_raw(const ExoFlight *f, int cx, int cz,
                             float fx, float fz, float yhint)
{
	const TestSpec *sp;
	float south, north, t;

	if (cx < 0 || cz < 0 || cx >= (int)f->map.w || cz >= (int)f->map.h)
		return (float)EXO_HEIGHT_VOID;
	sp = test_spec_id(test_id(f, cx, cz));
	if (sp->shape == TS_WATER)
		return (float)EXO_HEIGHT_VOID;
	if (sp->shape == TS_DUAL) {
		if (yhint >= EXO_TEST_DUAL_HI - 2.0f)
			return EXO_TEST_DUAL_HI;
		return EXO_TEST_DUAL_LO;
	}
	if (sp->shape == TS_FLAT || sp->shape == TS_ICE || sp->shape == TS_SPRING)
		return (float)sp->href;

	south = test_south(&f->map, cx, cz);
	north = south + test_delta(sp);
	if (south < 0.0f)
		south = 0.0f;
	if (north < 0.0f)
		north = 0.0f;
	/* fz=0 aresta norte (-Z), fz=1 aresta sul (+Z) */
	t = 1.0f - fz;
	(void)fx;
	if (sp->shape == TS_EXP) {
		float h = south + (north - south) * test_exp01(t);
		if (h < 0.0f)
			h = 0.0f;
		return h;
	}
	return south + (north - south) * t;
}

float exo_flight_surf_h(const ExoFlight *f, float wx, float wz)
{
	int cx, cz;
	float fx, fz;

	if (!f)
		return 0.0f;
	if (f->course != EXO_COURSE_TEST)
		return ground_h(f, wx, wz);
	cx = (int)floorf(wx / EXO_FLIGHT_CELL);
	cz = (int)floorf(wz / EXO_FLIGHT_CELL);
	fx = wx / EXO_FLIGHT_CELL - (float)cx;
	fz = wz / EXO_FLIGHT_CELL - (float)cz;
	return test_sample_raw(f, cx, cz, fx, fz, f->y);
}

void exo_flight_cell_hy(const ExoFlight *f, int cx, int cz, float hy[4])
{
	const TestSpec *sp;
	float south, north;

	if (!hy)
		return;
	hy[0] = hy[1] = hy[2] = hy[3] = 0.0f;
	if (!f || cx < 0 || cz < 0 ||
	    cx >= (int)f->map.w || cz >= (int)f->map.h)
		return;
	if (f->course != EXO_COURSE_TEST) {
		hy[0] = (float)exo_tilemap_cell_h(&f->map, cx, cz);
		hy[1] = hy[0];
		hy[2] = hy[0];
		hy[3] = hy[0];
		return;
	}
	sp = test_spec_id(test_id(f, cx, cz));
	if (sp->shape == TS_WATER)
		return;
	if (sp->shape == TS_DUAL) {
		hy[0] = hy[1] = hy[2] = hy[3] = EXO_TEST_DUAL_HI;
		return;
	}
	if (sp->shape == TS_FLAT || sp->shape == TS_ICE || sp->shape == TS_SPRING) {
		hy[0] = hy[1] = hy[2] = hy[3] = (float)sp->href;
		return;
	}
	south = test_south(&f->map, cx, cz);
	north = south + test_delta(sp);
	if (south < 0.0f)
		south = 0.0f;
	if (north < 0.0f)
		north = 0.0f;
	/* cantos: 0=(0,0)N  1=(1,0)N  2=(1,1)S  3=(0,1)S */
	hy[0] = north;
	hy[1] = north;
	hy[2] = south;
	hy[3] = south;
}

static void add_tree_cell(ExoFlight *f, int cx, int cz, float ox, float oz, float h)
{
	int i;
	if (f->tree_n >= EXO_FLIGHT_TREE_MAX)
		return;
	if (cx < 0 || cz < 0 || cx >= (int)f->map.w || cz >= (int)f->map.h)
		return;
	for (i = 0; i < f->tree_n; ++i)
		if (f->trees[i].cx == cx && f->trees[i].cz == cz &&
		    fabsf(f->trees[i].ox - ox) < 0.05f && fabsf(f->trees[i].oz - oz) < 0.05f)
			return;
	f->trees[f->tree_n].cx = cx;
	f->trees[f->tree_n].cz = cz;
	f->trees[f->tree_n].ox = ox;
	f->trees[f->tree_n].oz = oz;
	f->trees[f->tree_n].h = h;
	f->tree_n++;
}

static void place_demo_trees(ExoFlight *f)
{
	int mid = EXO_TILEMAP_MAX / 2;
	static const int P[][2] = {
		{ 0, -3 }, { -2, 1 }, { 2, 4 }, { 0, 8 },
		{ -3, 12 }, { 3, 15 }, { 0, 11 }, { -2, 16 }
	};
	int i;
	f->tree_n = 0;
	for (i = 0; i < 8; ++i)
		add_tree_cell(f, mid + P[i][0], mid + P[i][1], 0.5f, 0.5f,
		              28.0f + (float)(i % 4) * 4.0f);
}

static unsigned urand(unsigned *s)
{
	*s = *s * 1664525u + 1013904223u;
	return *s;
}

static int ss4_pad_ok(const ExoFlight *f, int cx, int cz)
{
	ExoTerrain t;
	if (cx < 2 || cz < 2 || cx >= (int)f->map.w - 2 || cz >= (int)f->map.h - 2)
		return 0;
	t = cell_ter(f, cx, cz);
	if (t != EXO_TER_NONE)
		return 0;
	return 1;
}

static void spawn_ss4_pad(ExoFlight *f)
{
	unsigned rng = 0xA5A5u ^ (unsigned)f->score * 7919u ^ (unsigned)f->map.tile_count;
	int tries, cx, cz;

	for (tries = 0; tries < 500; ++tries) {
		cx = 4 + (int)(urand(&rng) % (unsigned)(f->map.w - 8));
		cz = 4 + (int)(urand(&rng) % (unsigned)(f->map.h - 8));
		if (!ss4_pad_ok(f, cx, cz))
			continue;
		if (cx == f->goal_cx && cz == f->goal_cz)
			continue;
		f->goal_cx = cx;
		f->goal_cz = cz;
		return;
	}
	f->goal_cx = (int)f->map.w / 2;
	f->goal_cz = (int)f->map.h / 2 + 12;
}

static void setup_ss4_goals(ExoFlight *f)
{
	f->fake_n = 0;
	f->score = 0;
	f->goal_cx = -1;
	f->goal_cz = -1;
	spawn_ss4_pad(f);
}

static void setup_dp1_bushes(ExoFlight *f)
{
	int x, z;

	f->tree_n = 0;
	for (z = 0; z < (int)f->map.h - 1; ++z) {
		for (x = 0; x < (int)f->map.w - 1; ++x) {
			uint16_t tl = exo_tilemap_at(&f->map, x, z);
			uint16_t tr = exo_tilemap_at(&f->map, x + 1, z);
			uint16_t bl = exo_tilemap_at(&f->map, x, z + 1);
			uint16_t br = exo_tilemap_at(&f->map, x + 1, z + 1);
			if (exo_terrain_is_bush_tl(f->course, &f->map, tl, tr, bl, br))
				add_tree_cell(f, x, z, 1.0f, 1.0f, 34.0f);
		}
	}
}


static float finish_peak(const ExoFlight *f, int cx, int cz, float wx, float wz)
{
	float fx, fz, t;

	if (cell_ter(f, cx, cz) != EXO_TER_FINISH)
		return 0.0f;
	fx = wx / EXO_FLIGHT_CELL - (float)cx;
	fz = wz / EXO_FLIGHT_CELL - (float)cz;
	t = fabsf(fx - 0.5f);
	if (fabsf(fz - 0.5f) > t)
		t = fabsf(fz - 0.5f);
	t = 1.0f - 2.0f * t;
	if (t < 0.0f)
		t = 0.0f;
	return EXO_SNO_FINISH_PEAK * t;
}

static int ter_at(const ExoFlight *f, int cx, int cz)
{
	return exo_terrain_kind(f->course, &f->map, exo_tilemap_at(&f->map, cx, cz));
}

static float ground_h(const ExoFlight *f, float wx, float wz)
{
	int cx = (int)floorf(wx / EXO_FLIGHT_CELL);
	int cz = (int)floorf(wz / EXO_FLIGHT_CELL);
	float gx, gz, fx, fz;
	float h00, h10, h01, h11;
	int t00;

	if (!exo_tilemap_walkable(&f->map, cx, cz))
		return (float)EXO_HEIGHT_VOID;

	t00 = ter_at(f, cx, cz);
	if (f->course == EXO_COURSE_TEST)
		return test_sample_raw(f, cx, cz,
		                       wx / EXO_FLIGHT_CELL - (float)cx,
		                       wz / EXO_FLIGHT_CELL - (float)cz,
		                       f->y);
	/* parede/bumper: topo plano. nao interpolar com grama — isso
	   criava a rampa fantasma que derrubava a piloto. */
	if (t00 == EXO_TER_WALL)
		return EXO_FLIGHT_WALL_H + finish_peak(f, cx, cz, wx, wz);
	if (t00 == EXO_TER_BUMPER)
		return 0.0f;

	gx = wx / EXO_FLIGHT_CELL;
	gz = wz / EXO_FLIGHT_CELL;
	fx = gx - (float)cx;
	fz = gz - (float)cz;
	h00 = (float)exo_tilemap_cell_h(&f->map, cx, cz);
	{
		int t10 = ter_at(f, cx + 1, cz);
		int t01 = ter_at(f, cx, cz + 1);
		int t11 = ter_at(f, cx + 1, cz + 1);
		h10 = (exo_tilemap_walkable(&f->map, cx + 1, cz) &&
		       t10 != EXO_TER_WALL && t10 != EXO_TER_BUMPER &&
		       t10 != EXO_TER_WATER)
			? (float)exo_tilemap_cell_h(&f->map, cx + 1, cz) : h00;
		h01 = (exo_tilemap_walkable(&f->map, cx, cz + 1) &&
		       t01 != EXO_TER_WALL && t01 != EXO_TER_BUMPER &&
		       t01 != EXO_TER_WATER)
			? (float)exo_tilemap_cell_h(&f->map, cx, cz + 1) : h00;
		h11 = (exo_tilemap_walkable(&f->map, cx + 1, cz + 1) &&
		       t11 != EXO_TER_WALL && t11 != EXO_TER_BUMPER &&
		       t11 != EXO_TER_WATER)
			? (float)exo_tilemap_cell_h(&f->map, cx + 1, cz + 1) : h00;
	}
	{
		float h = h00 * (1.0f - fx) * (1.0f - fz) +
		          h10 * fx * (1.0f - fz) +
		          h01 * (1.0f - fx) * fz +
		          h11 * fx * fz;
		if (f->course == EXO_COURSE_DP1)
			h += finish_peak(f, cx, cz, wx, wz);
		return h;
	}
}

static void sno_heights(ExoTilemap *m)
{
	static uint8_t seen[EXO_TILEMAP_MAX * EXO_TILEMAP_MAX];
	static uint8_t qx[EXO_TILEMAP_MAX * EXO_TILEMAP_MAX];
	static uint8_t qz[EXO_TILEMAP_MAX * EXO_TILEMAP_MAX];
	int x, z, n, i, qn, ramp_minx, ramp_maxx;

	n = (int)m->w * (int)m->h;
	{
		int cols = m->atlas_cols ? (int)m->atlas_cols : EXO_SNO_COLS;
		if (cols < 1)
			cols = EXO_SNO_COLS;
		for (i = 0; i < n; ++i) {
			int tx, ty;
			uint16_t id = m->cells[i];
			seen[i] = 0;
			tx = (int)id % cols;
			ty = (int)id / cols;
			if (id == 0 || (ty == 0 && tx <= 20))
				m->height[i] = EXO_HEIGHT_VOID;
			else
				m->height[i] = EXO_SNO_TRACK_H;
		}
	}

	x = 56;
	z = 100;
	qn = 0;
	if (exo_tilemap_walkable(m, x, z)) {
		seen[z * (int)m->w + x] = 1;
		qx[qn] = (uint8_t)x;
		qz[qn] = (uint8_t)z;
		qn++;
	}
	i = 0;
	while (i < qn) {
		int cx = qx[i];
		int cz = qz[i];
		int k;
		static const int d[4][2] = { {1,0},{-1,0},{0,1},{0,-1} };
		i++;
		for (k = 0; k < 4; ++k) {
			int nx = cx + d[k][0];
			int nz = cz + d[k][1];
			if (!exo_tilemap_walkable(m, nx, nz))
				continue;
			if (seen[nz * (int)m->w + nx])
				continue;
			seen[nz * (int)m->w + nx] = 1;
			if (qn < EXO_TILEMAP_MAX * EXO_TILEMAP_MAX) {
				qx[qn] = (uint8_t)nx;
				qz[qn] = (uint8_t)nz;
				qn++;
			}
		}
	}

	ramp_minx = 127;
	ramp_maxx = 0;
	for (z = 0; z < (int)m->h; ++z) {
		for (x = 0; x < (int)m->w; ++x) {
			if (!exo_tilemap_walkable(m, x, z))
				continue;
			if (seen[z * (int)m->w + x])
				continue;
			if (x < ramp_minx) ramp_minx = x;
			if (x > ramp_maxx) ramp_maxx = x;
		}
	}
	if (ramp_maxx <= ramp_minx)
		return;
	for (z = 0; z < (int)m->h; ++z) {
		for (x = 0; x < (int)m->w; ++x) {
			float tt;
			if (!exo_tilemap_walkable(m, x, z) || seen[z * (int)m->w + x])
				continue;
			tt = (float)(x - ramp_minx) / (float)(ramp_maxx - ramp_minx);
			m->height[z * (int)m->w + x] = (int16_t)(EXO_SNO_RAMP_HI +
				(EXO_SNO_RAMP_LO - EXO_SNO_RAMP_HI) * tt + 0.5f);
		}
	}
}

/* U: vale a 0 no centro. +θ sobe para norte, −θ e o espelho para sul. */
static void apply_test_slope_column(ExoTilemap *m, int x)
{
	int zs[EXO_TILEMAP_MAX];
	int8_t deg[EXO_TILEMAP_MAX];
	int n = 0, i, z, split;

	for (z = 0; z < (int)m->h; ++z) {
		const TestSpec *sp = test_spec_id(exo_tilemap_at(m, x, z));
		if (sp->shape != TS_SLOPE)
			continue;
		zs[n] = z;
		deg[n] = sp->deg;
		n++;
	}
	if (n <= 0)
		return;

	/* zs crescente = norte → sul. split = 1o tile negativo (braco sul). */
	split = n;
	for (i = 0; i < n; ++i) {
		if (deg[i] < 0) {
			split = i;
			break;
		}
	}

	/* braco norte (+): vale a 0 na aresta sul do + mais proximo do centro */
	{
		float run = 0.0f;
		for (i = split - 1; i >= 0; --i) {
			float r = testdeg_rise(deg[i]);
			m->height[zs[i] * (int)m->w + x] = (int16_t)(run + 0.5f);
			run += r;
		}
	}
	/* braco sul (−): espelho — aresta norte = vale, sul sobe o mesmo rise */
	{
		float run = 0.0f;
		for (i = split; i < n; ++i) {
			float r = testdeg_rise(deg[i]);
			float south = run + r;
			m->height[zs[i] * (int)m->w + x] = (int16_t)(south + 0.5f);
			run = south;
		}
	}
}

static void apply_test_heights(ExoTilemap *m)
{
	int x, z;

	for (z = 0; z < (int)m->h; ++z) {
		for (x = 0; x < (int)m->w; ++x) {
			uint16_t id = exo_tilemap_at(m, x, z);
			const TestSpec *sp = test_spec_id(id);

			if (sp->shape == TS_WATER)
				m->height[z * (int)m->w + x] = EXO_HEIGHT_VOID;
			else if (sp->shape == TS_EXP)
				m->height[z * (int)m->w + x] = 0;
			else
				m->height[z * (int)m->w + x] = sp->href;
		}
	}
	for (x = 0; x < (int)m->w; ++x)
		apply_test_slope_column(m, x);
}

static void apply_heights(ExoFlight *f)
{
	int x, z;
	ExoTilemap *m = &f->map;

	if (f->course == EXO_COURSE_TEST) {
		apply_test_heights(m);
		return;
	}
	if (f->course == EXO_COURSE_SNO) {
		sno_heights(m);
		return;
	}
	/* SS4: chao 0, agua sem piso. Paredes/bumpers sao caixas. */
	if (f->course != EXO_COURSE_DP1) {
		for (z = 0; z < (int)m->h; ++z) {
			for (x = 0; x < (int)m->w; ++x) {
				ExoTerrain t = exo_terrain_kind(f->course, m,
				                                exo_tilemap_at(m, x, z));
				if (t == EXO_TER_WATER)
					m->height[z * (int)m->w + x] = EXO_HEIGHT_VOID;
				else
					m->height[z * (int)m->w + x] = 0;
			}
		}
		return;
	}
	for (z = 0; z < (int)m->h; ++z) {
		for (x = 0; x < (int)m->w; ++x) {
			int tx, ty;
			int16_t h = 0;
			ExoTerrain t = exo_terrain_kind(f->course, m,
			                                exo_tilemap_at(m, x, z));
			exo_terrain_xy(f->course, exo_tilemap_at(m, x, z), &tx, &ty);
			if (t == EXO_TER_WATER)
				h = EXO_HEIGHT_VOID;
			else if (t == EXO_TER_WALL)
				h = (int16_t)EXO_FLIGHT_WALL_H;
			else if ((tx == 11 && ty == 3) ||
			         (tx == 0 && ty == 4) ||
			         (tx == 4 && ty == 5))
				h = EXO_DP1_BRIDGE_H;
			m->height[z * (int)m->w + x] = h;
		}
	}
}

void exo_flight_reset_run(ExoFlight *f)
{
	float mx = (float)f->map.w * 0.5f;
	float mz = (float)f->map.h * 0.5f;
	f->run = EXO_RUN_WAIT;
	f->run_t = 0.0f;
	f->laps = 0;
	f->lap_chk = 0;
	f->score = 0;
	f->water_t = 0.0f;
	f->submerged = 0;
	f->bounce_cd = 0.0f;
	f->done_t = 0.0f;
	f->input_lock = 0;
	if (f->course == EXO_COURSE_TEST) {
		/* sul da faixa de teste (x~86 z~50), olhando norte (-Z) */
		f->x = 86.5f * EXO_FLIGHT_CELL;
		f->z = 50.5f * EXO_FLIGHT_CELL;
		f->yaw = 3.14159265f;
	} else if (f->course == EXO_COURSE_SNO) {
		f->x = 56.5f * EXO_FLIGHT_CELL;
		f->z = 100.5f * EXO_FLIGHT_CELL;
		f->yaw = 3.14159265f;
	} else {
		f->x = mx * EXO_FLIGHT_CELL;
		f->z = (mz - 8.0f) * EXO_FLIGHT_CELL;
		f->yaw = 0.0f;
	}
	f->y = ground_h(f, f->x, f->z);
	if (f->y < (float)EXO_HEIGHT_VOID / 2.0f)
		f->y = (float)EXO_SNO_TRACK_H;
	f->vy = 0.0f;
	f->speed = 0.0f;
	f->cruise = 0.0f;
	f->pitch = 0.0f;
	f->body_pitch = 0.0f;
	f->body_roll = 0.0f;
	f->cam_yaw = f->yaw;
	f->slope_roll = 0.0f;
	f->mode = EXO_FLIGHT_LOW;
	f->flying = 0;
	f->grounded = 1;
	f->charge = 0.0f;
	f->charge_armed = 0;
	f->spd_prev = 0.0f;
	f->spd_delta = 0.0f;
	if (f->course == EXO_COURSE_SS4)
		spawn_ss4_pad(f);
}

static void finish_course_setup(ExoFlight *f)
{
	f->goal_cx = -1;
	f->goal_cz = -1;
	if (f->course == EXO_COURSE_SS4)
		setup_ss4_goals(f);
	else if (f->course == EXO_COURSE_DP1)
		setup_dp1_bushes(f);
	else if (f->course == EXO_COURSE_SNO) {
		f->tree_n = 0;
		f->goal_cx = 75;
		f->goal_cz = 31;
	} else if (f->course == EXO_COURSE_TEST) {
		f->tree_n = 0;
		f->goal_cx = -1;
		f->goal_cz = -1;
	} else {
		place_demo_trees(f);
		f->goal_cx = (int)f->map.w / 2;
		f->goal_cz = (int)f->map.h / 2 + 18;
	}
	exo_flight_reset_run(f);
}

int exo_flight_load_map(ExoFlight *f, ExoCourse course, const void *etm, uint32_t size)
{
	f->course = course;
	f->tree_n = 0;
	f->fake_n = 0;
	if ((course == EXO_COURSE_TEST || course == EXO_COURSE_SNO) &&
	    etm && size &&
	    exo_tilemap_load(&f->map, etm, size)) {
		apply_heights(f);
		finish_course_setup(f);
		return 1;
	}
	if (course == EXO_COURSE_SNO && etm && size &&
	    exo_tilemap_load(&f->map, etm, size)) {
		apply_heights(f);
		finish_course_setup(f);
		return 1;
	}
	if (course == EXO_COURSE_SNO) {
		exo_sno_ensure();
		exo_tilemap_from_ids(&f->map, exo_sno_cell, EXO_TILEMAP_MAX,
		                     EXO_TILEMAP_MAX, 5, 5);
		apply_heights(f);
		finish_course_setup(f);
		return 1;
	}
	if (etm && size && exo_tilemap_load(&f->map, etm, size)) {
		apply_heights(f);
		finish_course_setup(f);
		return 1;
	}
	f->course = EXO_COURSE_DEMO;
	exo_tilemap_demo(&f->map);
	apply_heights(f);
	finish_course_setup(f);
	return 0;
}

void exo_flight_init(ExoFlight *f, ExoPilot pilot)
{
	memset(f, 0, sizeof(*f));
	f->pilot = pilot;
	f->cam_ref = EXO_CAM_SURFACE;
	f->focal = EXO_FLIGHT_FOCAL;
	f->render_r = EXO_FLIGHT_RENDER;
	f->draw_cap = 48 + EXO_FLIGHT_RENDER * EXO_FLIGHT_RENDER;
	exo_flight_load_map(f, EXO_COURSE_DEMO, NULL, 0);
}

void exo_flight_set_pilot(ExoFlight *f, ExoPilot pilot)
{
	f->pilot = pilot;
	if (f->speed > exo_flight_vmax(f))
		f->speed = exo_flight_vmax(f);
	if (f->cruise > exo_flight_vmax(f))
		f->cruise = exo_flight_vmax(f);
}

int exo_flight_is_goal(const ExoFlight *f, int cx, int cz)
{
	return f->course == EXO_COURSE_SS4 && cx == f->goal_cx && cz == f->goal_cz;
}

static bool blocked(const ExoFlight *f, float x, float z)
{
	int cx = (int)floorf(x / EXO_FLIGHT_CELL);
	int cz = (int)floorf(z / EXO_FLIGHT_CELL);
	int i;
	ExoTerrain t = cell_ter(f, cx, cz);
	int16_t hh = exo_tilemap_cell_h(&f->map, cx, cz);

	(void)hh;
	/* parede landable: bloqueia so o volume, o topo e piso */
	if (t == EXO_TER_WALL && f->y + EXO_FLIGHT_STEP_UP < EXO_FLIGHT_WALL_H)
		return true;
	if (t == EXO_TER_BUMPER && f->y + 2.0f < EXO_FLIGHT_BUMPER_H)
		return true;
	/* SNO: plataforma nao bloqueia XY — passa por baixo. */
	if (f->course == EXO_COURSE_DP1 &&
	    exo_tilemap_walkable(&f->map, cx, cz) &&
	    hh > (int16_t)(f->y + EXO_FLIGHT_STEP_UP) &&
	    t != EXO_TER_WATER)
		return true;
	for (i = 0; i < f->tree_n; ++i) {
		float tx = ((float)f->trees[i].cx + f->trees[i].ox) * EXO_FLIGHT_CELL;
		float tz = ((float)f->trees[i].cz + f->trees[i].oz) * EXO_FLIGHT_CELL;
		float dx = x - tx;
		float dz = z - tz;
		if (dx * dx + dz * dz < 64.0f && f->y < f->trees[i].h - 2.0f)
			return true;
	}
	return false;
}

static int is_bumper(const ExoFlight *f, float x, float z)
{
	int cx = (int)floorf(x / EXO_FLIGHT_CELL);
	int cz = (int)floorf(z / EXO_FLIGHT_CELL);
	return cell_ter(f, cx, cz) == EXO_TER_BUMPER && f->y < 8.0f;
}

static void bounce_off(ExoFlight *f, float hx, float hz)
{
	int cx = (int)floorf(hx / EXO_FLIGHT_CELL);
	int cz = (int)floorf(hz / EXO_FLIGHT_CELL);
	float bx = ((float)cx + 0.5f) * EXO_FLIGHT_CELL;
	float bz = ((float)cz + 0.5f) * EXO_FLIGHT_CELL;
	float dx = f->x - bx;
	float dz = f->z - bz;
	float mag = sqrtf(dx * dx + dz * dz);

	if (f->bounce_cd > 0.0f)
		return;
	if (mag < 0.05f) {
		dx = -sinf(f->yaw);
		dz = -cosf(f->yaw);
		mag = 1.0f;
	}
	dx /= mag;
	dz /= mag;
	/* Sonic CD SS: angulo do centro, repulsao cresce com a velocidade */
	{
		float push = 14.0f + f->speed * 0.10f;
		if (push > 36.0f)
			push = 36.0f;
		f->x += dx * push;
		f->z += dz * push;
	}
	f->yaw = atan2f(dx, dz);
	if (f->speed < 48.0f)
		f->speed = 48.0f;
	f->cruise = f->speed;
	f->bounce_cd = 0.28f;
}

static void clamp_map(ExoFlight *f)
{
	float minp = EXO_FLIGHT_CELL * 1.05f;
	float maxx = (float)f->map.w * EXO_FLIGHT_CELL - minp;
	float maxz = (float)f->map.h * EXO_FLIGHT_CELL - minp;
	if (f->x < minp) f->x = minp;
	if (f->z < minp) f->z = minp;
	if (f->x > maxx) f->x = maxx;
	if (f->z > maxz) f->z = maxz;
}

void exo_flight_tick(ExoFlight *f, const ExoInput *in, float dt)
{
	const ExoPilotStats *s = exo_pilot_stats(f->pilot);
	float sx = in ? in->stick_x : 0.0f;
	float sy = in ? in->stick_y : 0.0f;
	float turn_low = s->turn_low;
	float turn_high = s->turn_high;
	float coast = s->coast;
	ExoTerrain here0;
	int hold_a = in && (in->held & EXO_BTN_A);
	int hold_y = in && (in->held & EXO_BTN_Y);
	int tap_b = in && (in->down & EXO_BTN_B);
	int tap_zr = in && (in->down & EXO_BTN_ZR);
	int hold_l = in && (in->held & EXO_BTN_L);
	int hold_r = in && (in->held & EXO_BTN_R);
	float nx, nz, step, cap;
	float vmax = exo_flight_vmax(f);
	float fwd_x, fwd_z, rgt_x, rgt_z;
	float strafe;
	int stable;
	ExoTerrain here;

	if (dt <= 0.0f || dt > 0.05f)
		dt = 1.0f / 60.0f;

	f->cell_x = (int)floorf(f->x / EXO_FLIGHT_CELL);
	f->cell_z = (int)floorf(f->z / EXO_FLIGHT_CELL);
	here0 = cell_ter(f, f->cell_x, f->cell_z);
	if (f->grounded && here0 == EXO_TER_ICE) {
		coast *= EXO_TEST_ICE_COAST;
		turn_low *= EXO_TEST_ICE_TURN;
		turn_high *= EXO_TEST_ICE_TURN;
	}

	if (f->run == EXO_RUN_DONE) {
		f->input_lock = 1;
		f->done_t += dt;
		if (f->speed > 0.0f) {
			f->speed -= s->coast * 2.4f * dt;
			if (f->speed < 0.0f)
				f->speed = 0.0f;
		}
		f->cruise = f->speed;
		sx = 0.0f;
		sy = 0.0f;
		hold_a = 0;
		hold_y = 0;
		tap_b = 0;
		tap_zr = 0;
		hold_l = 0;
		hold_r = 0;
	} else {
		f->input_lock = 0;
	}

	f->pad_x = sx;
	f->pad_y = sy;
	f->draw_cap = 48 + f->render_r * f->render_r;
	if (f->draw_cap > 720)
		f->draw_cap = 720;
	if (f->spring_cd > 0.0f) {
		f->spring_cd -= dt;
		if (f->spring_cd < 0.0f)
			f->spring_cd = 0.0f;
	}
	if (f->bounce_cd > 0.0f) {
		f->bounce_cd -= dt;
		if (f->bounce_cd < 0.0f)
			f->bounce_cd = 0.0f;
	}

	if (tap_zr)
		f->cam_ref = (f->cam_ref == EXO_CAM_SURFACE) ? EXO_CAM_PILOT : EXO_CAM_SURFACE;

	if (hold_y)
		f->charge += dt / EXO_FLIGHT_CHARGE_SEC;
	else
		f->charge -= dt / EXO_FLIGHT_CHARGE_DECAY;
	f->charge = clampf(f->charge, 0.0f, 1.0f);
	if (f->charge >= 1.0f)
		f->charge_armed = 1;
	else if (f->charge <= 0.0f)
		f->charge_armed = 0;
	stable = f->charge_armed && f->charge > 0.0f;

	if (f->mode == EXO_FLIGHT_LOW && f->speed >= EXO_FLIGHT_HYST_ENTER)
		f->mode = EXO_FLIGHT_HIGH;
	else if (f->mode == EXO_FLIGHT_HIGH && f->speed <= EXO_FLIGHT_HYST_LEAVE)
		f->mode = EXO_FLIGHT_LOW;

	f->flying = (f->mode == EXO_FLIGHT_HIGH && !f->grounded);
	strafe = (hold_r ? 1.0f : 0.0f) - (hold_l ? 1.0f : 0.0f);
	/* LOW: L/R (e C-Stick) giram a camara. Stick X continua lateral. */
	if (f->mode == EXO_FLIGHT_LOW) {
		float cam_turn = strafe;
		if (in && fabsf(in->cstick_x) > 0.08f)
			cam_turn = in->cstick_x;
		f->cam_yaw += cam_turn * turn_low * dt;
	}

	if (f->mode == EXO_FLIGHT_HIGH || f->flying) {
		fwd_x = sinf(f->yaw);
		fwd_z = cosf(f->yaw);
		rgt_x =  cosf(f->yaw);
		rgt_z = -sinf(f->yaw);

		if (f->flying) {
			if (fabsf(sy) > 0.08f)
				f->body_pitch += (-sy) * EXO_FLIGHT_PITCH_RATE * dt;
			if (fabsf(sx) > 0.08f)
				f->body_roll += sx * EXO_FLIGHT_PITCH_RATE * dt;
			while (f->body_pitch > 3.14159265f)
				f->body_pitch -= 6.2831853f;
			while (f->body_pitch < -3.14159265f)
				f->body_pitch += 6.2831853f;
			while (f->body_roll > 3.14159265f)
				f->body_roll -= 6.2831853f;
			while (f->body_roll < -3.14159265f)
				f->body_roll += 6.2831853f;
			{
				float want_cam = clampf(f->body_pitch,
				                        -EXO_FLIGHT_PITCH_MAX,
				                        EXO_FLIGHT_PITCH_MAX);
				f->pitch += (want_cam - f->pitch) * clampf(dt * 10.0f, 0.0f, 1.0f);
			}
			/* leme L/R + um pouco de yaw pelo roll */
			f->yaw += strafe * turn_high * dt;
			f->yaw += sinf(f->body_roll) * turn_high * 0.45f * dt;
		} else {
			f->yaw += sx * turn_high * dt;
			f->body_roll += (0.0f - f->body_roll) * clampf(dt * 3.0f, 0.0f, 1.0f);
		}
		if (!f->flying && !stable) {
			f->body_pitch += (0.0f - f->body_pitch) * clampf(dt * 2.4f, 0.0f, 1.0f);
			f->pitch += (0.0f - f->pitch) * clampf(dt * 2.4f, 0.0f, 1.0f);
		}

		if (stable) {
			if (hold_y)
				f->cruise += s->accel * dt;
			else if (hold_a)
				f->cruise -= coast * 1.6f * dt;
			f->cruise = clampf(f->cruise, 0.0f, vmax);
			f->speed = f->cruise;
		} else {
			if (hold_y && f->speed < vmax)
				f->speed += s->accel * dt;
			else if (hold_a)
				f->speed -= coast * 1.8f * dt;
			else
				f->speed -= coast * dt;
			f->speed = clampf(f->speed, 0.0f, vmax);
			f->cruise = f->speed;
		}

		step = f->speed * cosf(f->body_pitch) * dt;
		if (step > EXO_FLIGHT_CELL * 0.45f)
			step = EXO_FLIGHT_CELL * 0.45f;
		nx = f->x + fwd_x * step;
		nz = f->z + fwd_z * step;
		f->wish_x = fwd_x;
		f->wish_z = fwd_z;
	} else {
		/* LOW: frente = camara, X e L/R = lateral. Camara nao gira com o stick. */
		float mx, mz, mag;
		float cam_fx = sinf(f->cam_yaw);
		float cam_fz = cosf(f->cam_yaw);
		float cam_rx =  cosf(f->cam_yaw);
		float cam_rz = -sinf(f->cam_yaw);

		if (!stable) {
			f->body_pitch += (0.0f - f->body_pitch) * clampf(dt * 2.4f, 0.0f, 1.0f);
			f->pitch += (0.0f - f->pitch) * clampf(dt * 2.4f, 0.0f, 1.0f);
		}
		f->yaw = f->cam_yaw;
		fwd_x = cam_fx;
		fwd_z = cam_fz;
		rgt_x = cam_rx;
		rgt_z = cam_rz;
		mx = cam_fx * sy + cam_rx * sx;
		mz = cam_fz * sy + cam_rz * sx;
		mag = sqrtf(mx * mx + mz * mz);
		if (mag > 1.0f) {
			mx /= mag;
			mz /= mag;
			mag = 1.0f;
		}
		f->wish_x = mx;
		f->wish_z = mz;

		if (stable) {
			if (hold_y)
				f->cruise += s->accel * dt;
			else if (hold_a)
				f->cruise -= coast * 1.6f * dt;
			/* carga armada pode furar o tecto LOW e entrar em HIGH */
			f->cruise = clampf(f->cruise, 0.0f, vmax);
			f->speed = f->cruise;
		} else {
			int on_ice = (here0 == EXO_TER_ICE && f->grounded);
			/* Tails-like: andar/correr so chega a run, nao a vmax */
			cap = s->run;
			if (mag < 0.35f)
				cap = s->walk + (s->run - s->walk) * (mag / 0.35f);
			if (mag > 0.05f) {
				float target = cap * mag;
				if (target > s->run)
					target = s->run;
				if (f->speed < target)
					f->speed += s->accel * dt;
				else
					f->speed -= coast * dt;
				if (!on_ice) {
					if (f->speed > s->run)
						f->speed += (s->run - f->speed) * clampf(dt * 3.0f, 0.0f, 1.0f);
				} else if (f->speed > vmax) {
					f->speed = vmax;
				}
			} else {
				f->speed -= coast * dt;
				if (f->speed < 0.0f)
					f->speed = 0.0f;
			}
			f->cruise = f->speed;
		}

		step = f->speed * dt;
		if (step > EXO_FLIGHT_CELL * 0.45f)
			step = EXO_FLIGHT_CELL * 0.45f;
		{
			int on_ice = (here0 == EXO_TER_ICE && f->grounded);
			if (mag > 0.05f || (stable && f->speed > 0.5f) ||
			    (on_ice && f->speed > 0.4f)) {
				float ux = (mag > 0.05f) ? mx : fwd_x;
				float uz = (mag > 0.05f) ? mz : fwd_z;
				nx = f->x + ux * step;
				nz = f->z + uz * step;
			} else {
				nx = f->x;
				nz = f->z;
			}
		}
	}

	if (is_bumper(f, nx, f->z)) {
		bounce_off(f, nx, f->z);
		nx = f->x;
	} else if (!blocked(f, nx, f->z))
		f->x = nx;
	else
		f->speed *= 0.5f;

	if (is_bumper(f, f->x, nz)) {
		bounce_off(f, f->x, nz);
		nz = f->z;
	} else if (!blocked(f, f->x, nz))
		f->z = nz;
	else
		f->speed *= 0.5f;

	clamp_map(f);
	f->move_x = f->wish_x;
	f->move_z = f->wish_z;

	f->cell_x = (int)floorf(f->x / EXO_FLIGHT_CELL);
	f->cell_z = (int)floorf(f->z / EXO_FLIGHT_CELL);
	here = cell_ter(f, f->cell_x, f->cell_z);

	if (f->run != EXO_RUN_DONE) {
		if (f->grounded && here == EXO_TER_BOOST_N)
			f->z -= EXO_FLIGHT_BOOST * dt;
		else if (f->grounded && here == EXO_TER_BOOST_S)
			f->z += EXO_FLIGHT_BOOST * dt;
		else if (f->grounded && here == EXO_TER_BOOST_W)
			f->x -= EXO_FLIGHT_BOOST * dt;
		else if (f->grounded && here == EXO_TER_BOOST_E)
			f->x += EXO_FLIGHT_BOOST * dt;
		else if (here == EXO_TER_BOOST) {
			if (f->grounded) {
				f->speed += EXO_FLIGHT_BOOST * 0.55f * dt;
				if (f->speed > vmax)
					f->speed = vmax;
				f->cruise = f->speed;
			}
			f->charge += dt / 0.55f;
			if (f->charge > 1.0f)
				f->charge = 1.0f;
			if (f->charge >= 1.0f)
				f->charge_armed = 1;
		}
	}

	if (tap_b && !f->submerged) {
		if (f->mode == EXO_FLIGHT_HIGH) {
			f->vy = s->jump * 1.25f;
			f->grounded = 0;
			f->flying = 1;
		} else if (f->grounded) {
			f->vy = s->jump;
			f->grounded = 0;
			f->flying = 0;
		}
	}

	/* TEST: [0,1] e id 6 = EXO_TILE_SPRING do demo. Nao usar
	   exo_tilemap_spring() neste curso. Mola so com os pes no chao. */
	if (f->spring_cd <= 0.0f && f->grounded && !f->submerged &&
	    here == EXO_TER_SPRING) {
		f->vy = (f->course == EXO_COURSE_TEST)
			? EXO_FLIGHT_SPRING_TEST : EXO_FLIGHT_SPRING_VY;
		f->grounded = 0;
		f->spring_cd = 0.40f;
		f->flying = 1;
		if (f->mode != EXO_FLIGHT_HIGH && f->speed > EXO_FLIGHT_HYST_LEAVE)
			f->mode = EXO_FLIGHT_HIGH;
	} else if (f->course != EXO_COURSE_TEST && f->spring_cd <= 0.0f &&
	           exo_tilemap_spring(&f->map, f->cell_x, f->cell_z)) {
		f->vy = EXO_FLIGHT_SPRING_VY;
		f->grounded = 0;
		f->spring_cd = 0.40f;
		f->flying = 1;
	}

	if (here == EXO_TER_TRAP) {
		if (f->flying || f->y > 2.0f)
			f->vy -= 160.0f * dt;
		else if (f->speed > 8.0f) {
			f->speed = 0.0f;
			f->cruise = 0.0f;
			f->vy = -20.0f;
			f->y = 0.0f;
			f->grounded = 1;
			f->flying = 0;
			f->mode = EXO_FLIGHT_LOW;
		}
	}

	if (f->flying) {
		float climb = f->speed * sinf(f->body_pitch);
		if (stable)
			f->vy += (climb - f->vy) * clampf(dt * 8.0f, 0.0f, 1.0f);
		else {
			f->vy += (climb - f->vy) * clampf(dt * 4.0f, 0.0f, 1.0f);
			f->vy -= EXO_FLIGHT_GRAV * dt;
		}
	} else {
		f->vy -= EXO_FLIGHT_GRAV * dt;
	}

	f->y += f->vy * dt;

	{
		float gh = ground_h(f, f->x, f->z);
		int walk = exo_tilemap_walkable(&f->map, f->cell_x, f->cell_z) &&
		           gh > (float)EXO_HEIGHT_VOID / 2.0f;
		int wet = (here == EXO_TER_WATER);
		int two_surf = (f->course == EXO_COURSE_SNO) ||
		               (here == EXO_TER_DUAL);
		int was_g = f->grounded;
		float prev_y = f->y - f->vy * dt;

		if (f->course == EXO_COURSE_SNO && f->y < EXO_SNO_RESET_Y) {
			exo_flight_reset_run(f);
			return;
		}
		if (f->y < -48.0f) {
			exo_flight_reset_run(f);
			return;
		}

		if (wet) {
			/* agua nao e piso: cai, nada, sai ao pisar terra */
			f->submerged = 1;
			f->flying = 0;
			f->grounded = 0;
			if (f->y > -1.0f && f->vy <= 0.0f)
				f->y = -1.0f;
			if (f->y < -14.0f) {
				f->y = -14.0f;
				if (f->vy < 0.0f)
					f->vy = 0.0f;
			}
			f->speed *= 0.94f;
			f->water_t += dt;
			if ((f->course == EXO_COURSE_DP1 ||
			     f->course == EXO_COURSE_TEST) &&
			    f->water_t >= EXO_FLIGHT_WATER_DROWN)
				exo_flight_reset_run(f);
		} else if (walk && f->submerged && f->y < gh) {
			/* sai da agua para qualquer tile de superficie */
			f->y = gh;
			f->vy = 0.0f;
			f->grounded = 1;
			f->submerged = 0;
			f->water_t = 0.0f;
			f->flying = 0;
		} else {
			f->submerged = 0;
			if (f->course != EXO_COURSE_SS4 || !wet)
				f->water_t = 0.0f;
			if (two_surf && walk && here == EXO_TER_DUAL &&
			    prev_y + EXO_FLIGHT_BODY <= EXO_TEST_DUAL_HI &&
			    f->y + EXO_FLIGHT_BODY > EXO_TEST_DUAL_HI &&
			    f->vy > 0.0f) {
				f->y = EXO_TEST_DUAL_HI - EXO_FLIGHT_BODY;
				f->vy = 0.0f;
				f->grounded = 0;
			} else if (two_surf && walk && here != EXO_TER_DUAL &&
			    prev_y + EXO_FLIGHT_BODY <= gh &&
			    f->y + EXO_FLIGHT_BODY > gh &&
			    f->y < gh && f->vy > 0.0f) {
				/* teto da plataforma flutuante */
				f->y = gh - EXO_FLIGHT_BODY;
				f->vy = 0.0f;
				f->grounded = 0;
			} else if (walk &&
			           ((prev_y >= gh && f->y <= gh) ||
			            (was_g && f->course == EXO_COURSE_TEST &&
			             f->y > gh - 18.0f && f->y < gh + 10.0f &&
			             f->vy <= 12.0f) ||
			            (!f->flying &&
			             f->y >= gh - 3.0f && f->y <= gh + 2.5f &&
			             f->vy <= 4.0f))) {
				/* HIGH voando so pousa ao cruzar o plano — sem
				   bolha de 6u que cola a piloto no chao. */
				f->y = gh;
				if (f->vy < 0.0f)
					f->vy = 0.0f;
				f->grounded = 1;
				if (f->mode != EXO_FLIGHT_HIGH)
					f->flying = 0;
				{
					float hx = ground_h(f, f->x + 8.0f, f->z) -
					           ground_h(f, f->x - 8.0f, f->z);
					float hz = ground_h(f, f->x, f->z + 8.0f) -
					           ground_h(f, f->x, f->z - 8.0f);
					float want_p;
					int on_wall = (here == EXO_TER_WALL);
					if (hx > 80.0f || hx < -80.0f) hx = 0.0f;
					if (hz > 80.0f || hz < -80.0f) hz = 0.0f;
					if (on_wall) {
						hx = 0.0f;
						hz = 0.0f;
					}
					if (!on_wall && here != EXO_TER_ICE &&
					    (f->course == EXO_COURSE_SNO ||
					     f->course == EXO_COURSE_DP1 ||
					     f->course == EXO_COURSE_TEST)) {
						f->x -= hx * EXO_FLIGHT_GRAV * 0.045f * dt;
						f->z -= hz * EXO_FLIGHT_GRAV * 0.045f * dt;
					}
					want_p = atanf((hx * sinf(f->yaw) +
					                hz * cosf(f->yaw)) / 16.0f);
					want_p = clampf(want_p, -EXO_FLIGHT_PITCH_MAX,
					                EXO_FLIGHT_PITCH_MAX);
					f->pitch += (want_p - f->pitch) *
					            clampf(dt * 8.0f, 0.0f, 1.0f);
					f->body_pitch += (want_p - f->body_pitch) *
					                 clampf(dt * 8.0f, 0.0f, 1.0f);
					{
						float rgt_x =  cosf(f->yaw);
						float rgt_z = -sinf(f->yaw);
						float want_r = atanf((hx * rgt_x + hz * rgt_z) / 16.0f);
						want_r = clampf(want_r, -0.70f, 0.70f);
						f->slope_roll += (want_r - f->slope_roll) *
						                 clampf(dt * 8.0f, 0.0f, 1.0f);
					}
				}
			} else {
				f->grounded = 0;
				f->slope_roll += (0.0f - f->slope_roll) * clampf(dt * 4.0f, 0.0f, 1.0f);
				if (f->y > EXO_FLIGHT_Y_MAX) {
					f->y = EXO_FLIGHT_Y_MAX;
					if (f->vy > 0.0f)
						f->vy = 0.0f;
				}
			}
		}
	}

	f->flying = (f->mode == EXO_FLIGHT_HIGH && !f->grounded && !f->submerged);

	{
		float want_bank = 0.0f;
		if (f->flying)
			want_bank = clampf(f->body_roll, -EXO_FLIGHT_BANK_MAX,
			                   EXO_FLIGHT_BANK_MAX);
		else if (f->mode == EXO_FLIGHT_HIGH)
			want_bank = sx * EXO_FLIGHT_BANK_MAX;
		f->bank += (want_bank - f->bank) * clampf(dt * 10.0f, 0.0f, 1.0f);
	}

	f->focal = EXO_FLIGHT_FOCAL;
	{
		float pin, eye, dist;
		if (f->mode == EXO_FLIGHT_HIGH || f->flying) {
			pin = EXO_FLIGHT_PIN_FLY;
			eye = 24.0f;
			dist = 48.0f;
		} else {
			pin = EXO_FLIGHT_PIN_LOW;
			eye = 42.0f;
			dist = 58.0f;
		}
		f->cam_dist = dist;
		f->cam_h = f->y + eye;
		f->horizon = pin - eye * (EXO_FLIGHT_FOCAL / dist)
		           - sinf(f->pitch) * 52.0f;
	}

	{
		float cy;
		if (f->mode != EXO_FLIGHT_LOW)
			f->cam_yaw += (f->yaw - f->cam_yaw) * clampf(dt * 6.0f, 0.0f, 1.0f);
		cy = f->cam_yaw;
		f->cam_x = f->x - sinf(cy) * f->cam_dist;
		f->cam_z = f->z - cosf(cy) * f->cam_dist;
	}
	f->spd_delta = f->speed - f->spd_prev;
	f->spd_prev = f->speed;

	f->cell_x = (int)floorf(f->x / EXO_FLIGHT_CELL);
	f->cell_z = (int)floorf(f->z / EXO_FLIGHT_CELL);
	here = cell_ter(f, f->cell_x, f->cell_z);

	if (f->run == EXO_RUN_WAIT && f->speed > 4.0f)
		f->run = EXO_RUN_GO;
	if (f->run == EXO_RUN_GO) {
		float tscale = 1.0f;
		if (f->course == EXO_COURSE_SS4 && here == EXO_TER_WATER)
			tscale = 4.0f;
		f->run_t += dt * tscale;

		if (f->course == EXO_COURSE_DP1) {
			if (here != EXO_TER_FINISH)
				f->lap_chk = 1;
			if (f->lap_chk && here == EXO_TER_FINISH && f->grounded) {
				f->laps++;
				f->lap_chk = 0;
				if (f->laps >= 3) {
					f->run = EXO_RUN_DONE;
					if (f->best_t <= 0.0f || f->run_t < f->best_t)
						f->best_t = f->run_t;
				}
			}
		} else if (f->course == EXO_COURSE_SNO) {
			if (here != EXO_TER_FINISH)
				f->lap_chk = 1;
			if (f->lap_chk && here == EXO_TER_FINISH && f->grounded) {
				f->laps++;
				f->lap_chk = 0;
				if (f->laps >= 3) {
					f->run = EXO_RUN_DONE;
					if (f->best_t <= 0.0f || f->run_t < f->best_t)
						f->best_t = f->run_t;
				}
			}
		} else if (f->course == EXO_COURSE_SS4) {
			if (f->grounded && f->y < 1.5f &&
			    f->cell_x == f->goal_cx && f->cell_z == f->goal_cz) {
				f->score++;
				if (f->score >= EXO_FLIGHT_SS4_NEED) {
					f->run = EXO_RUN_DONE;
					if (f->best_t <= 0.0f || f->run_t < f->best_t)
						f->best_t = f->run_t;
				} else {
					spawn_ss4_pad(f);
				}
			}
		} else if (f->grounded && f->y < 1.5f &&
		           f->cell_x == f->goal_cx && f->cell_z == f->goal_cz) {
			f->run = EXO_RUN_DONE;
			if (f->best_t <= 0.0f || f->run_t < f->best_t)
				f->best_t = f->run_t;
		}
	}
}

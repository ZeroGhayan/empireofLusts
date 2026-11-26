#include "exo/terrain.h"
#include <stddef.h>

static int s_cols;

int exo_terrain_cols(ExoCourse course, const ExoTilemap *m)
{
	int c;

	if (m && m->atlas_cols)
		c = (int)m->atlas_cols;
	else if (course == EXO_COURSE_SS4)
		c = EXO_SS4_COLS;
	else if (course == EXO_COURSE_DP1)
		c = EXO_DP1_COLS;
	else if (course == EXO_COURSE_SNO)
		c = EXO_SNO_COLS;
	else if (course == EXO_COURSE_TEST)
		c = EXO_TEST_ATLAS_COLS;
	else
		c = 8;
	if (c < 1)
		c = 1;
	s_cols = c;
	return c;
}

void exo_terrain_xy(ExoCourse course, uint16_t id, int *tx, int *ty)
{
	int cols = s_cols ? s_cols : exo_terrain_cols(course, NULL);
	if (cols < 1)
		cols = 1;
	if (tx) *tx = (int)id % cols;
	if (ty) *ty = (int)id / cols;
}

static int at(int cols, uint16_t id, int x, int y)
{
	if (cols < 1)
		return 0;
	return ((int)id % cols) == x && ((int)id / cols) == y;
}

static int row_range(int cols, uint16_t id, int y0, int y1)
{
	int y;
	if (cols < 1)
		return 0;
	y = (int)id / cols;
	return y >= y0 && y <= y1;
}

static int xr(int cols, uint16_t id, int x0, int x1, int y)
{
	int x, yy;
	if (cols < 1)
		return 0;
	x = (int)id % cols;
	yy = (int)id / cols;
	return yy == y && x >= x0 && x <= x1;
}

static ExoTerrain kind_dp1(int c, uint16_t id)
{
	if (at(c, id, 0, 1) || at(c, id, 4, 6))
		return EXO_TER_WATER;
	if (at(c, id, 0, 0) || at(c, id, 1, 0) || at(c, id, 2, 0) || at(c, id, 3, 0))
		return EXO_TER_WALL;
	if (at(c, id, 0, 10))
		return EXO_TER_FINISH;
	return EXO_TER_NONE;
}

static ExoTerrain kind_ss4(int c, uint16_t id)
{
	if (row_range(c, id, 0, 4) ||
	    at(c, id, 2, 5) || at(c, id, 1, 6) ||
	    xr(c, id, 10, 12, 6) || xr(c, id, 15, 18, 6) ||
	    xr(c, id, 0, 2, 7) || at(c, id, 15, 7) ||
	    at(c, id, 3, 8) || at(c, id, 4, 8) || at(c, id, 9, 8) || at(c, id, 14, 8) ||
	    xr(c, id, 4, 6, 9) || xr(c, id, 13, 15, 9) ||
	    at(c, id, 8, 10) || at(c, id, 0, 11) || at(c, id, 4, 11) ||
	    xr(c, id, 16, 18, 11) ||
	    at(c, id, 0, 12) || at(c, id, 9, 12) ||
	    xr(c, id, 10, 13, 14) ||
	    at(c, id, 14, 15) || at(c, id, 15, 15) ||
	    at(c, id, 5, 16) || at(c, id, 10, 16) || at(c, id, 13, 16) ||
	    at(c, id, 0, 17) || at(c, id, 1, 17))
		return EXO_TER_WATER;

	if (at(c, id, 0, 5) || at(c, id, 1, 5) || at(c, id, 3, 5))
		return EXO_TER_BUMPER;

	if (at(c, id, 13, 5) || at(c, id, 14, 5) || at(c, id, 2, 6) ||
	    at(c, id, 3, 6) || at(c, id, 6, 11))
		return EXO_TER_BOOST_S;
	if (at(c, id, 16, 5) || at(c, id, 17, 5) || at(c, id, 4, 6) ||
	    at(c, id, 5, 6) || at(c, id, 15, 11))
		return EXO_TER_BOOST_W;
	if (at(c, id, 14, 14) || at(c, id, 15, 14) || at(c, id, 1, 15) ||
	    at(c, id, 2, 15) || at(c, id, 4, 10) || at(c, id, 18, 14))
		return EXO_TER_BOOST_N;
	if (xr(c, id, 15, 18, 17) || at(c, id, 2, 10))
		return EXO_TER_BOOST_E;

	if (at(c, id, 11, 5) || at(c, id, 16, 10))
		return EXO_TER_TRAP;
	if (at(c, id, 6, 13))
		return EXO_TER_SPRING;

	return EXO_TER_NONE;
}

static ExoTerrain kind_test(int c, uint16_t id)
{
	int tx, ty;

	if (c < 1)
		c = EXO_TEST_ATLAS_COLS;
	tx = (int)id % c;
	ty = (int)id / c;
	if (tx == 1 && ty == 0)
		return EXO_TER_WATER;
	if (tx == 0 && ty == 2)
		return EXO_TER_ICE;
	if (tx == 0 && ty == 1)
		return EXO_TER_DUAL;
	if (tx == 5 && ty == 4)
		return EXO_TER_SPRING;
	return EXO_TER_NONE;
}

ExoTerrain exo_terrain_kind(ExoCourse course, const ExoTilemap *m, uint16_t id)
{
	int c = exo_terrain_cols(course, m);

	if (course == EXO_COURSE_TEST)
		return kind_test(c, id);
	if (course == EXO_COURSE_DP1)
		return kind_dp1(c, id);
	if (course == EXO_COURSE_SS4)
		return kind_ss4(c, id);
	if (course == EXO_COURSE_SNO) {
		int tx, ty;
		tx = (int)id % c;
		ty = (int)id / c;
		/* nuvem [0,0]..[20,0]: so camada de baixo, nao e pista */
		if (ty == 0 && tx <= 20)
			return EXO_TER_NONE;
		if (id == 0)
			return EXO_TER_NONE;
		if (tx == 20 && ty == 21)
			return EXO_TER_FINISH;
		if ((ty == 16 && (tx == 17 || tx == 18)) ||
		    (tx == 8 && ty == 18) ||
		    (tx == 12 && ty == 17))
			return EXO_TER_BOOST;
		return EXO_TER_NONE;
	}
	if (id == EXO_TILE_WALL)
		return EXO_TER_WALL;
	if (id == EXO_TILE_SPRING)
		return EXO_TER_SPRING;
	if (id == EXO_TILE_PAD)
		return EXO_TER_FINISH;
	return EXO_TER_NONE;
}

int exo_terrain_is_bush_tl(ExoCourse course, const ExoTilemap *m,
                           uint16_t tl, uint16_t tr,
                           uint16_t bl, uint16_t br)
{
	int c = exo_terrain_cols(course, m);
	if (course != EXO_COURSE_DP1)
		return 0;
	if (at(c, tl, 9, 0) && at(c, tr, 10, 0) && at(c, bl, 3, 1) && at(c, br, 4, 1))
		return 1;
	if (at(c, tl, 8, 1) && at(c, tr, 9, 1) && at(c, bl, 7, 2) && at(c, br, 8, 2))
		return 1;
	return 0;
}

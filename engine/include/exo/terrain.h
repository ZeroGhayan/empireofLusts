#ifndef EXO_TERRAIN_H
#define EXO_TERRAIN_H

#include "exo/flight.h"

#define EXO_SS4_COLS 19
#define EXO_DP1_COLS 16

typedef enum ExoTerrain {
	EXO_TER_NONE = 0,
	EXO_TER_WATER,
	EXO_TER_WALL,
	EXO_TER_BUMPER,
	EXO_TER_FINISH,
	EXO_TER_BUSH,
	EXO_TER_SPRING,
	EXO_TER_TRAP,
	EXO_TER_BOOST_N,
	EXO_TER_BOOST_S,
	EXO_TER_BOOST_E,
	EXO_TER_BOOST_W,
	EXO_TER_BOOST
} ExoTerrain;

int        exo_terrain_cols(ExoCourse course, const ExoTilemap *m);
void       exo_terrain_xy(ExoCourse course, uint16_t id, int *tx, int *ty);
ExoTerrain exo_terrain_kind(ExoCourse course, const ExoTilemap *m, uint16_t id);
int        exo_terrain_is_bush_tl(ExoCourse course, const ExoTilemap *m,
                                 uint16_t tl, uint16_t tr,
                                 uint16_t bl, uint16_t br);

#endif

#ifndef EXO_ELEM_H
#define EXO_ELEM_H

#include "exo/types.h"

typedef enum ExoElem {
	EXO_NORMAL = 0,
	EXO_FIRE,
	EXO_WATER,
	EXO_ELECTRIC,
	EXO_GRASS,
	EXO_ICE,
	EXO_FIGHTING,
	EXO_POISON,
	EXO_GROUND,
	EXO_FLYING,
	EXO_PSYCHIC,
	EXO_BUG,
	EXO_ROCK,
	EXO_GHOST,
	EXO_DRAGON,
	EXO_DARK,
	EXO_STEEL,
	EXO_FAIRY,
	EXO_SHADOW,
	EXO_LIGHT,
	EXO_CHAOS,
	EXO_ANGEL,
	EXO_DEMON,
	EXO_AURUM,
	EXO_CRYSTAL,
	EXO_ELEM_COUNT
} ExoElem;

uint16_t    exo_elem_mul(ExoElem atk, ExoElem def);
uint16_t 	exo_elem_mul2(ExoElem atk, ExoElem def1, ExoElem def2);
const char *exo_elem_name(ExoElem e);
ExoElem     exo_elem_next(ExoElem e);

#endif
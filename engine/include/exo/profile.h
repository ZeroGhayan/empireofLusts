#ifndef EXO_PROFILE_H
#define EXO_PROFILE_H

#include "exo/combat.h"

typedef struct ExoProfile {
	const char *name;
	int16_t     hp;
	uint8_t     meter_max;
	uint8_t     meter_cost;
	uint8_t     meter_slots;
	bool        taunt_is_counter;
	uint8_t     taunt_frames;
	uint16_t    taunt_cd;
	ExoElem     type;
	ExoElem     type2;
} ExoProfile;

unsigned exo_profile_count(void);
const ExoProfile *exo_profile(unsigned id);
void exo_fighter_apply_profile(ExoFighter *f, const ExoProfile *p);

#endif

#include "exo/profile.h"

static const ExoProfile k_profiles[] = {
	{
		.name = "AEGIS",
		.hp = 100,
		.meter_max = 100,
		.meter_cost = 50,
		.meter_slots = 1,
		.taunt_is_counter = true,
		.taunt_frames = 48,
		.taunt_cd = 180,
		.type = EXO_FIRE,
		.type2 = EXO_FIRE
	},
	{
		.name = "ECHO",
		.hp = 120,
		.meter_max = 80,
		.meter_cost = 40,
		.meter_slots = 1,
		.taunt_is_counter = false,
		.taunt_frames = 40,
		.taunt_cd = 60,
		.type = EXO_WATER,
		.type2 = EXO_WATER
	}
};

unsigned exo_profile_count(void)
{
	return (unsigned)(sizeof(k_profiles) / sizeof(k_profiles[0]));
}

const ExoProfile *exo_profile(unsigned id)
{
	unsigned n = exo_profile_count();

	if (n == 0)
		return 0;
	return &k_profiles[id % n];
}

void exo_fighter_apply_profile(ExoFighter *f, const ExoProfile *p)
{
	if (!f || !p)
		return;
	f->hp_max = p->hp;
	f->hp = p->hp;
	f->taunt_is_counter = p->taunt_is_counter;
	f->type = p->type;
	f->type2 = p->type2;
}

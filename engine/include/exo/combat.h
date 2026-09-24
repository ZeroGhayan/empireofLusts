#ifndef EXO_COMBAT_H
#define EXO_COMBAT_H

#include "exo/body.h"
#include "exo/elem.h"

typedef struct ExoRect {
	float x, y, w, h;
} ExoRect;

typedef enum ExoPhase {
	EXO_PHASE_IDLE = 0,
	EXO_PHASE_STARTUP,
	EXO_PHASE_ACTIVE,
	EXO_PHASE_RECOVERY,
	EXO_PHASE_HITSTUN,
	EXO_PHASE_BLOCK,
	EXO_PHASE_BLOCKSTUN,
	EXO_PHASE_GRAB,
	EXO_PHASE_GRABBED,
	EXO_PHASE_TAUNT
} ExoPhase;

typedef enum ExoHeight {
	EXO_HT_MID = 0,
	EXO_HT_LOW,
	EXO_HT_OVER
} ExoHeight;

typedef struct ExoMove {
	uint8_t startup;
	uint8_t active;
	uint8_t recovery;
	float   box_x, box_y, box_w, box_h;
	float   kb_x, kb_y;
	uint8_t hitstun;
	uint8_t damage;
	bool    air_ok;
	ExoElem type;
	ExoHeight height;
	bool grab;
	uint8_t meter_hit;
	uint8_t chip;
} ExoMove;

typedef struct ExoFighter {
	ExoBody        body;
	ExoPhase       phase;
	uint8_t        timer;
	bool           hit_done;
	const ExoMove *move;
	int16_t        hp;
	int16_t        hp_max;
	ExoElem type;
	ExoElem type2;
	bool crouched;
	bool cancel;
	uint16_t taunt_cd;
	uint16_t taunt_lock;
	bool taunt_is_counter;
} ExoFighter;

bool exo_aabb(const ExoRect *a, const ExoRect *b);

void exo_fighter_init(ExoFighter *f, float x, float y, float w, float h);
bool exo_fighter_attack(ExoFighter *f, const ExoMove *move);
bool exo_fighter_taunt(ExoFighter *f, uint8_t frames, uint16_t cooldown);
void exo_fighter_taunt_riposte(ExoFighter *taunter, ExoFighter *fool);
void exo_fighter_tick(ExoFighter *f);
bool exo_fighter_can_act(const ExoFighter *f);
bool exo_fighter_busy(const ExoFighter *f);
bool exo_fighter_blocking(const ExoFighter *f);
void exo_fighter_guard(ExoFighter *f, bool hold);
bool exo_fighter_hitbox(const ExoFighter *f, ExoRect *out);
ExoRect exo_fighter_hurt(const ExoFighter *f);
bool exo_fighter_in_front(const ExoFighter *att, const ExoFighter *vic);
void exo_fighter_apply_hit(ExoFighter *victim, const ExoFighter *attacker);
void exo_fighter_apply_block(ExoFighter *victim, const ExoFighter *attacker);
void exo_fighter_crouch(ExoFighter *f, bool on);
void exo_fighter_apply_grab(ExoFighter *victim, ExoFighter *attacker);

#endif

#ifndef EXO_BODY_H
#define EXO_BODY_H

#include "exo/types.h"

typedef struct ExoBody {
	float x, y, w, h;
	float vx, vy;
	int   facing;   /* -1 esquerda, +1 direita */
	bool  grounded;
} ExoBody;

void exo_body_init(ExoBody *b, float x, float y, float w, float h);
void exo_body_integrate(ExoBody *b, float dt, float gravity, float ground_y);
void exo_body_jump(ExoBody *b, float speed);
void exo_body_face(ExoBody *self, const ExoBody *other);
void exo_body_separate(ExoBody *a, ExoBody *b, float min_x, float max_w);

#endif
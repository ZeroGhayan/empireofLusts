#include "exo/body.h"

void exo_body_init(ExoBody *b, float x, float y, float w, float h)
{
	b->x = x;
	b->y = y;
	b->w = w;
	b->h = h;
	b->vx = 0.0f;
	b->vy = 0.0f;
	b->facing = 1;
	b->grounded = false;
}

void exo_body_integrate(ExoBody *b, float dt, float gravity, float ground_y)
{
	b->vy += gravity * dt;
	b->x  += b->vx * dt;
	b->y  += b->vy * dt;

	float feet = b->y + b->h;
	if (feet >= ground_y) {
		b->y = ground_y - b->h;
		b->vy = 0.0f;
		b->grounded = true;
	} else {
		b->grounded = false;
	}
}

void exo_body_jump(ExoBody *b, float speed)
{
	if (!b->grounded)
		return;
	b->vy = -speed;
	b->grounded = false;
}

void exo_body_face(ExoBody *self, const ExoBody *other)
{
	float dx = other->x - self->x;
	if (dx > 1.0f)
		self->facing = 1;
	else if (dx < -1.0f)
		self->facing = -1;
}

void exo_body_separate(ExoBody *a, ExoBody *b, float min_x, float max_w)
{
	float ax2 = a->x + a->w;
	float bx2 = b->x + b->w;
	float ay2 = a->y + a->h;
	float by2 = b->y + b->h;
	float overlap;

	if (ax2 <= b->x || bx2 <= a->x)
		return;
	if (ay2 <= b->y || by2 <= a->y)
		return;

	if (a->x + a->w * 0.5f <= b->x + b->w * 0.5f)
		overlap = ax2 - b->x;
	else
		overlap = bx2 - a->x;

	overlap *= 0.5f;
	if (a->x + a->w * 0.5f <= b->x + b->w * 0.5f) {
		a->x -= overlap;
		b->x += overlap;
	} else {
		a->x += overlap;
		b->x -= overlap;
	}

	if (a->x < min_x) a->x = min_x;
	if (b->x < min_x) b->x = min_x;
	if (a->x > max_w - a->w) a->x = max_w - a->w;
	if (b->x > max_w - b->w) b->x = max_w - b->w;
}

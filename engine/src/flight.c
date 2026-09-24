#include "exo/flight.h"
#include <math.h>
#include <string.h>

static const ExoPilotStats STATS[2] = {
	{ /* Shirammy: tecto alto, acelera só com A */
		.vmax      = 295.0f,
		.walk      = 14.0f,
		.accel     = 48.0f,
		.coast     = 22.0f,
		.turn_low  = 2.1f,
		.turn_high = 1.5f,
		.jump      = 34.0f,
		.hud_scale = 1062.0f / 295.0f,
		.hud_unit  = "Gm/h",
		.name      = "Shirammy"
	},
	{ /* Rexxi: tecto baixo, controlo fino */
		.vmax      = 48.0f,
		.walk      = 9.0f,
		.accel     = 20.0f,
		.coast     = 28.0f,
		.turn_low  = 2.4f,
		.turn_high = 2.0f,
		.jump      = 38.0f,
		.hud_scale = 400.0f / 48.0f,
		.hud_unit  = "km/h",
		.name      = "Rexxi"
	}
};

const ExoPilotStats *exo_pilot_stats(ExoPilot p)
{
	if ((unsigned)p > (unsigned)EXO_PILOT_REXXI)
		p = EXO_PILOT_SHIRAMMY;
	return &STATS[p];
}

void exo_flight_init(ExoFlight *f, ExoPilot pilot)
{
	memset(f, 0, sizeof(*f));
	exo_tilemap_demo(&f->map);
	f->pilot = pilot;
	f->mode = EXO_FLIGHT_LOW;
	f->x = (EXO_TILEMAP_MAX * 0.5f) * EXO_FLIGHT_CELL;
	f->z = (EXO_TILEMAP_MAX * 0.5f - 8.0f) * EXO_FLIGHT_CELL;
	f->y = 0.0f;
	f->grounded = 1;
	f->cam_h = 48.0f;
	f->cam_dist = 56.0f;
	f->horizon = 70.0f;
}

void exo_flight_set_pilot(ExoFlight *f, ExoPilot pilot)
{
	f->pilot = pilot;
	if (f->speed > exo_pilot_stats(pilot)->vmax)
		f->speed = exo_pilot_stats(pilot)->vmax;
}

static bool blocked(const ExoFlight *f, float x, float z)
{
	int cx = (int)floorf(x / EXO_FLIGHT_CELL);
	int cz = (int)floorf(z / EXO_FLIGHT_CELL);
	return exo_tilemap_solid(&f->map, cx, cz);
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

static float clampf(float v, float lo, float hi)
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

void exo_flight_tick(ExoFlight *f, const ExoInput *in, float dt)
{
	const ExoPilotStats *s = exo_pilot_stats(f->pilot);
	float sx = in ? in->stick_x : 0.0f;
	float sy = in ? in->stick_y : 0.0f;
	int hold_a = in && (in->held & EXO_BTN_A);
	int tap_b = in && (in->down & EXO_BTN_B);
	float nx, nz, step, cap;
	float turn = 0.0f;

	if (dt <= 0.0f || dt > 0.05f)
		dt = 1.0f / 60.0f;

	if (in) {
		if (in->held & EXO_BTN_L) turn += 1.0f;
		if (in->held & EXO_BTN_R) turn -= 1.0f;
		turn -= in->cstick_x;
	}

	if (f->mode == EXO_FLIGHT_HIGH) {
		f->yaw += (-sx) * s->turn_high * dt;
		if (hold_a)
			f->speed += s->accel * dt;
		else
			f->speed -= s->coast * dt;
		f->speed = clampf(f->speed, 0.0f, s->vmax);
		step = f->speed * dt;
		if (step > EXO_FLIGHT_CELL * 0.45f)
			step = EXO_FLIGHT_CELL * 0.45f;
		nx = f->x + sinf(f->yaw) * step;
		nz = f->z + cosf(f->yaw) * step;
	} else {
		float mx, mz, mag;
		float c = sinf(f->yaw);
		float sn = cosf(f->yaw);

		f->yaw += turn * s->turn_low * dt;
		/* movimento relativo à câmera; NÃO alinha o yaw ao stick */
		mx =  c * sx + sn * sy;
		mz = -sn * sx + c * sy;
		mag = sqrtf(mx * mx + mz * mz);
		if (mag > 1.0f) {
			mx /= mag;
			mz /= mag;
			mag = 1.0f;
		}
		cap = hold_a ? s->vmax : s->walk;
		if (mag > 0.05f) {
			float target = cap * mag;
			if (f->speed < target)
				f->speed += s->accel * dt;
			else
				f->speed -= s->coast * dt;
			f->speed = clampf(f->speed, 0.0f, target);
			step = f->speed * dt;
			if (step > EXO_FLIGHT_CELL * 0.45f)
				step = EXO_FLIGHT_CELL * 0.45f;
			nx = f->x + mx * step;
			nz = f->z + mz * step;
		} else {
			f->speed -= s->coast * dt;
			if (f->speed < 0.0f)
				f->speed = 0.0f;
			nx = f->x;
			nz = f->z;
		}
	}

	if (!blocked(f, nx, f->z))
		f->x = nx;
	else
		f->speed *= 0.5f;
	if (!blocked(f, f->x, nz))
		f->z = nz;
	else
		f->speed *= 0.5f;

	clamp_map(f);

	if (tap_b && f->grounded) {
		f->vy = s->jump;
		f->grounded = 0;
	}
	f->vy -= EXO_FLIGHT_GRAV * dt;
	f->y += f->vy * dt;
	if (f->y <= 0.0f) {
		f->y = 0.0f;
		f->vy = 0.0f;
		f->grounded = 1;
	}

	if (f->mode == EXO_FLIGHT_LOW && f->speed >= EXO_FLIGHT_HYST_ENTER)
		f->mode = EXO_FLIGHT_HIGH;
	else if (f->mode == EXO_FLIGHT_HIGH && f->speed <= EXO_FLIGHT_HYST_LEAVE)
		f->mode = EXO_FLIGHT_LOW;

	if (f->mode == EXO_FLIGHT_HIGH) {
		f->cam_h = 22.0f + f->y * 0.35f;
		f->cam_dist = 38.0f;
		f->horizon = 88.0f;
	} else {
		f->cam_h = 52.0f + f->y * 0.45f;
		f->cam_dist = 62.0f;
		f->horizon = 68.0f;
	}

	f->cam_x = f->x - sinf(f->yaw) * f->cam_dist;
	f->cam_z = f->z - cosf(f->yaw) * f->cam_dist;
	f->cell_x = (int)floorf(f->x / EXO_FLIGHT_CELL);
	f->cell_z = (int)floorf(f->z / EXO_FLIGHT_CELL);
}

float exo_flight_hud_speed(const ExoFlight *f)
{
	return f->speed * exo_pilot_stats(f->pilot)->hud_scale;
}

void exo_flight_eye_offset(const ExoFlight *f, float slider, int eye_sign,
                           float *ox, float *oy)
{
	(void)f;
	*ox = slider * 2.4f * (float)eye_sign;
	*oy = 0.0f;
}

int exo_flight_project(const ExoFlight *f, float wx, float wz,
                       float eye_x, float *sx, float *sy)
{
	float dx, dz, lx, lz, k;

	dx = wx - f->cam_x;
	dz = wz - f->cam_z;
	lx =  dx * cosf(f->yaw) - dz * sinf(f->yaw);
	lz =  dx * sinf(f->yaw) + dz * cosf(f->yaw);
	lx += eye_x;
	if (lz < EXO_FLIGHT_NEAR)
		return 0;
	k = 200.0f / lz;
	*sx = 200.0f + lx * k;
	*sy = f->horizon + (f->cam_h - f->y) * k;
	return 1;
}

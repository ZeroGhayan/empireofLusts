#include "exo/flight.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const ExoPilotStats STATS[2] = {
	{
		.vmax      = 295.0f,
		.accel     = 38.0f,
		.brake     = 70.0f,
		.turn_low  = 2.4f,
		.turn_high = 1.6f,
		.hud_scale = 1062.0f / 295.0f,
		.hud_unit  = "Gm/h",
		.name      = "Shirammy"
	},
	{
		/* 400 km/h no HUD. Teto real menor; aceleração maior. */
		.vmax      = 48.0f,
		.accel     = 90.0f,
		.brake     = 80.0f,
		.turn_low  = 2.8f,
		.turn_high = 2.1f,
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
	const ExoPilotStats *s;

	memset(f, 0, sizeof(*f));

	exo_tilemap_demo(&f->map);
	f->pilot = pilot;
	f->mode = EXO_FLIGHT_LOW;
	s = exo_pilot_stats(pilot);
	(void)s;
	/* centro da cruz da demo, olhando +Z */
	f->x = (EXO_TILEMAP_MAX * 0.5f) * EXO_FLIGHT_CELL;
	f->z = (EXO_TILEMAP_MAX * 0.5f - 8.0f) * EXO_FLIGHT_CELL;
	f->yaw = 0.0f;
	f->speed = 0.0f;
	f->cam_h = 42.0f;
	f->cam_dist = 48.0f;
	f->horizon = 78.0f;
}

void exo_flight_set_pilot(ExoFlight *f, ExoPilot pilot)
{
	f->pilot = pilot;
	if (f->speed > exo_pilot_stats(pilot)->vmax)
		f->speed = exo_pilot_stats(pilot)->vmax;
}

static void wrap_pos(ExoFlight *f)
{
	float world = (float)f->map.w * EXO_FLIGHT_CELL;
	if (world <= 0.0f)
		return;
	while (f->x < 0.0f)      f->x += world;
	while (f->x >= world)    f->x -= world;
	world = (float)f->map.h * EXO_FLIGHT_CELL;
	while (f->z < 0.0f)      f->z += world;
	while (f->z >= world)    f->z -= world;
}

static bool blocked(const ExoFlight *f, float x, float z)
{
	int cx = (int)(x / EXO_FLIGHT_CELL);
	int cz = (int)(z / EXO_FLIGHT_CELL);
	return exo_tilemap_solid(&f->map, cx, cz);
}

void exo_flight_tick(ExoFlight *f, const ExoInput *in, float dt)
{
	const ExoPilotStats *s = exo_pilot_stats(f->pilot);
	float sx = in ? in->stick_x : 0.0f;
	float sy = in ? in->stick_y : 0.0f;
	float c, sn, nx, nz, want;
	int high;

	if (dt <= 0.0f || dt > 0.05f)
		dt = 1.0f / 60.0f;

	high = (f->mode == EXO_FLIGHT_HIGH);

	if (high) {
		/* F-Zero: avanço automático, stick X governa. */
		want = s->vmax;
		if (in && (in->held & EXO_BTN_B))
			want = 0.0f;
		if (f->speed < want)
			f->speed += s->accel * dt;
		else
			f->speed -= s->brake * dt;
		if (f->speed < 0.0f)
			f->speed = 0.0f;
		if (f->speed > s->vmax)
			f->speed = s->vmax;
		f->yaw += (-sx) * s->turn_high * dt;
		nx = f->x + sinf(f->yaw) * f->speed * dt;
		nz = f->z + cosf(f->yaw) * f->speed * dt;
	} else {
		/* SM64 / Sonic 3D: câmera-relativo, sem avanço forçado. */
		float mx, mz, mag;
		float cam_yaw = f->yaw;
		float turn = 0.0f;

		if (in) {
			if (in->held & EXO_BTN_L) turn += 1.0f;
			if (in->held & EXO_BTN_R) turn -= 1.0f;
			turn -= in->cstick_x;
		}
		f->yaw += turn * s->turn_low * dt;

		c  = sinf(cam_yaw);
		sn = cosf(cam_yaw);
		/* stick_y > 0 = frente */
		mx =  c * sx + sn * sy;
		mz = -sn * sx + c * sy;
		mag = sqrtf(mx * mx + mz * mz);
		if (mag > 1.0f) {
			mx /= mag;
			mz /= mag;
			mag = 1.0f;
		}
		if (mag > 0.05f) {
			float target = s->vmax * 0.35f * mag;
			if (f->speed < target)
				f->speed += s->accel * dt;
			else
				f->speed -= s->brake * 0.5f * dt;
			if (f->speed > target)
				f->speed = target;
			nx = f->x + mx * f->speed * dt;
			nz = f->z + mz * f->speed * dt;
			/* alinha o nariz com o movimento */
			f->yaw = atan2f(mx, mz);
		} else {
			f->speed -= s->brake * dt;
			if (f->speed < 0.0f)
				f->speed = 0.0f;
			nx = f->x;
			nz = f->z;
		}
	}

	if (!blocked(f, nx, f->z))
		f->x = nx;
	else
		f->speed *= 0.4f;
	if (!blocked(f, f->x, nz))
		f->z = nz;
	else
		f->speed *= 0.4f;

	wrap_pos(f);

	if (f->mode == EXO_FLIGHT_LOW && f->speed >= EXO_FLIGHT_HYST_ENTER)
		f->mode = EXO_FLIGHT_HIGH;
	else if (f->mode == EXO_FLIGHT_HIGH && f->speed <= EXO_FLIGHT_HYST_LEAVE)
		f->mode = EXO_FLIGHT_LOW;

	if (f->mode == EXO_FLIGHT_HIGH) {
		f->cam_h = 16.0f;
		f->cam_dist = 26.0f;
		f->horizon = 92.0f;
	} else {
		f->cam_h = 42.0f;
		f->cam_dist = 52.0f;
		f->horizon = 74.0f;
	}

	f->cam_x = f->x - sinf(f->yaw) * f->cam_dist;
	f->cam_z = f->z - cosf(f->yaw) * f->cam_dist;
	f->cell_x = (int)(f->x / EXO_FLIGHT_CELL);
	f->cell_z = (int)(f->z / EXO_FLIGHT_CELL);
}

float exo_flight_hud_speed(const ExoFlight *f)
{
	return f->speed * exo_pilot_stats(f->pilot)->hud_scale;
}

void exo_flight_eye_offset(const ExoFlight *f, float slider, int eye_sign,
                           float *ox, float *oy)
{
	float iod = slider * 2.8f * (float)eye_sign;
	(void)f;
	*ox = iod;
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
	if (lz < 4.0f)
		return 0;
	k = 220.0f / lz;
	*sx = 200.0f + lx * k;
	*sy = f->horizon + f->cam_h * k;
	if (*sy < -40.0f || *sy > 280.0f)
		return 0;
	return 1;
}

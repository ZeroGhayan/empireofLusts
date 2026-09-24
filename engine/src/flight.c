#include "exo/flight.h"
#include <math.h>
#include <string.h>

static const ExoPilotStats STATS[2] = {
	{
		.vmax = 295.0f, .walk = 14.0f, .accel = 48.0f, .coast = 22.0f,
		.turn_low = 2.1f, .turn_high = 1.5f, .jump = 34.0f,
		.hud_scale = 1062.0f / 295.0f, .hud_unit = "Gm/h", .name = "Shirammy"
	},
	{
		.vmax = 48.0f, .walk = 9.0f, .accel = 20.0f, .coast = 28.0f,
		.turn_low = 2.4f, .turn_high = 2.0f, .jump = 38.0f,
		.hud_scale = 400.0f / 48.0f, .hud_unit = "km/h", .name = "Rexxi"
	}
};

const ExoPilotStats *exo_pilot_stats(ExoPilot p)
{
	if ((unsigned)p > (unsigned)EXO_PILOT_REXXI)
		p = EXO_PILOT_SHIRAMMY;
	return &STATS[p];
}

static float clampf(float v, float lo, float hi)
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

float exo_flight_vmax(const ExoFlight *f)
{
	if (f->pilot == EXO_PILOT_REXXI)
		return f->rexxi_vmax;
	return exo_pilot_stats(f->pilot)->vmax;
}

void exo_flight_init(ExoFlight *f, ExoPilot pilot)
{
	memset(f, 0, sizeof(*f));
	exo_tilemap_demo(&f->map);
	f->pilot = pilot;
	f->mode = EXO_FLIGHT_LOW;
	f->x = (EXO_TILEMAP_MAX * 0.5f) * EXO_FLIGHT_CELL;
	f->z = (EXO_TILEMAP_MAX * 0.5f - 8.0f) * EXO_FLIGHT_CELL;
	f->grounded = 1;
	f->cam_h = 48.0f;
	f->cam_dist = 56.0f;
	f->horizon = 70.0f;
	f->render_r = EXO_FLIGHT_RENDER;
	f->draw_cap = EXO_FLIGHT_DRAW_CAP;
	f->rexxi_vmax = EXO_FLIGHT_REXXI_VMAX;
}

void exo_flight_set_pilot(ExoFlight *f, ExoPilot pilot)
{
	f->pilot = pilot;
	if (f->speed > exo_flight_vmax(f))
		f->speed = exo_flight_vmax(f);
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

void exo_flight_tick(ExoFlight *f, const ExoInput *in, float dt)
{
	const ExoPilotStats *s = exo_pilot_stats(f->pilot);
	float sx = in ? in->stick_x : 0.0f;
	float sy = in ? in->stick_y : 0.0f;
	int hold_a = in && (in->held & EXO_BTN_A);
	int tap_b = in && (in->down & EXO_BTN_B);
	float nx, nz, step, cap, turn = 0.0f;
	float vmax = exo_flight_vmax(f);
	float fwd_x = sinf(f->yaw);
	float fwd_z = cosf(f->yaw);
	float rgt_x =  cosf(f->yaw);
	float rgt_z = -sinf(f->yaw);

	if (dt <= 0.0f || dt > 0.05f)
		dt = 1.0f / 60.0f;

	f->pad_x = sx;
	f->pad_y = sy;

	if (in) {
		if (in->held & EXO_BTN_L) turn += 1.0f;
		if (in->held & EXO_BTN_R) turn -= 1.0f;
		turn -= in->cstick_x;
	}

	if (f->mode == EXO_FLIGHT_HIGH) {
		f->yaw += (-sx) * s->turn_high * dt;
		fwd_x = sinf(f->yaw);
		fwd_z = cosf(f->yaw);
		if (hold_a)
			f->speed += s->accel * dt;
		else
			f->speed -= s->coast * dt;
		f->speed = clampf(f->speed, 0.0f, vmax);
		step = f->speed * dt;
		if (step > EXO_FLIGHT_CELL * 0.45f)
			step = EXO_FLIGHT_CELL * 0.45f;
		nx = f->x + fwd_x * step;
		nz = f->z + fwd_z * step;
		f->wish_x = fwd_x;
		f->wish_z = fwd_z;
	} else {
		float mx, mz, mag;

		f->yaw += turn * s->turn_low * dt;
		fwd_x = sinf(f->yaw);
		fwd_z = cosf(f->yaw);
		rgt_x =  cosf(f->yaw);
		rgt_z = -sinf(f->yaw);

		/* +stick_y = frente da personagem, +stick_x = strafe direita */
		mx = rgt_x * sx + fwd_x * sy;
		mz = rgt_z * sx + fwd_z * sy;
		mag = sqrtf(mx * mx + mz * mz);
		if (mag > 1.0f) {
			mx /= mag;
			mz /= mag;
			mag = 1.0f;
		}
		f->wish_x = mx;
		f->wish_z = mz;
		cap = hold_a ? vmax : s->walk;
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

	f->move_x = nx - (f->x == nx ? f->x : f->x); /* keep last wish as heading */
	(void)0;
	f->move_x = f->wish_x;
	f->move_z = f->wish_z;

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
		f->cam_h = 26.0f + f->y * 0.35f;
		f->cam_dist = 40.0f;
		f->horizon = 84.0f;
	} else {
		f->cam_h = 50.0f + f->y * 0.45f;
		f->cam_dist = 58.0f;
		f->horizon = 66.0f;
	}

	f->cam_x = f->x - sinf(f->yaw) * f->cam_dist;
	f->cam_z = f->z - cosf(f->yaw) * f->cam_dist;
	f->cell_x = (int)floorf(f->x / EXO_FLIGHT_CELL);
	f->cell_z = (int)floorf(f->z / EXO_FLIGHT_CELL);
}

float exo_flight_hud_speed(const ExoFlight *f)
{
	float vmax = exo_pilot_stats(f->pilot)->vmax;
	float hud_full;

	if (vmax <= 0.0f)
		return 0.0f;
	/* escala HUD usa o vmax de design (48 / 295), não o slider */
	hud_full = exo_pilot_stats(f->pilot)->hud_scale;
	return f->speed * hud_full;
}

void exo_flight_eye_offset(const ExoFlight *f, float slider, int eye_sign,
                           float *ox, float *oy)
{
	(void)f;
	*ox = slider * 2.4f * (float)eye_sign;
	*oy = 0.0f;
}

static void to_cam(const ExoFlight *f, float wx, float wz, float eye,
                   float *lx, float *lz)
{
	float dx = wx - f->cam_x;
	float dz = wz - f->cam_z;
	*lx =  dx * cosf(f->yaw) - dz * sinf(f->yaw) + eye;
	*lz =  dx * sinf(f->yaw) + dz * cosf(f->yaw);
}

static void cam_to_screen(const ExoFlight *f, float lx, float lz,
                          float *sx, float *sy)
{
	float k = EXO_FLIGHT_FOCAL / lz;
	*sx = 200.0f + lx * k;
	*sy = f->horizon + (f->cam_h - f->y) * k;
}

int exo_flight_project(const ExoFlight *f, float wx, float wz,
                       float eye_x, float *sx, float *sy)
{
	float lx, lz;
	to_cam(f, wx, wz, eye_x, &lx, &lz);
	if (lz < EXO_FLIGHT_NEAR)
		return 0;
	cam_to_screen(f, lx, lz, sx, sy);
	return 1;
}

int exo_flight_tile_visible(const ExoFlight *f, float wx, float wz,
                            float cell, float eye_x, float far_z, float *out_lz)
{
	float lx, lz;
	float half = cell * 0.5f;
	float lim;

	to_cam(f, wx + half, wz + half, eye_x, &lx, &lz);
	if (out_lz)
		*out_lz = lz;
	if (lz < EXO_FLIGHT_NEAR * 0.35f)
		return 0;
	if (lz > far_z)
		return 0;
	/* ~fov horizontal da tela 400 com focal 210, folga para o tamanho da tile */
	lim = lz * (200.0f / EXO_FLIGHT_FOCAL) + cell * 0.85f;
	if (lx > lim || lx < -lim)
		return 0;
	return 1;
}

int exo_flight_clip_quad(const ExoFlight *f, float wx, float wz, float cell,
                         float eye_x, float sx[6], float sy[6], int *nv)
{
	float lx[4], lz[4];
	float olx[8], olz[8];
	int i, n = 0;
	float wxv[4], wzv[4];

	wxv[0] = wx;        wzv[0] = wz;
	wxv[1] = wx + cell; wzv[1] = wz;
	wxv[2] = wx + cell; wzv[2] = wz + cell;
	wxv[3] = wx;        wzv[3] = wz + cell;

	for (i = 0; i < 4; ++i)
		to_cam(f, wxv[i], wzv[i], eye_x, &lx[i], &lz[i]);

	for (i = 0; i < 4; ++i) {
		int j = (i + 1) & 3;
		int in_i = lz[i] >= EXO_FLIGHT_NEAR;
		int in_j = lz[j] >= EXO_FLIGHT_NEAR;
		if (in_i) {
			olx[n] = lx[i];
			olz[n] = lz[i];
			n++;
		}
		if (in_i != in_j) {
			float denom = lz[j] - lz[i];
			float t = (fabsf(denom) < 1e-5f) ? 0.0f : (EXO_FLIGHT_NEAR - lz[i]) / denom;
			if (t < 0.0f) t = 0.0f;
			if (t > 1.0f) t = 1.0f;
			olx[n] = lx[i] + t * (lx[j] - lx[i]);
			olz[n] = EXO_FLIGHT_NEAR;
			n++;
		}
	}
	if (n < 3) {
		*nv = 0;
		return 0;
	}
	if (n > 6) n = 6;
	for (i = 0; i < n; ++i)
		cam_to_screen(f, olx[i], olz[i], &sx[i], &sy[i]);
	*nv = n;
	return 1;
}

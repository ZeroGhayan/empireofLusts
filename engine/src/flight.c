#include "exo/flight.h"
#include <math.h>
#include <string.h>

static const ExoPilotStats STATS[2] = {
	{
		.vmax = 380.0f, .walk = 18.0f, .accel = 70.0f, .coast = 24.0f,
		.turn_low = 2.2f, .turn_high = 1.65f, .jump = 36.0f, .climb = 58.0f,
		.hud_max = EXO_FLIGHT_HUD_SHI, .hud_unit = "Gm/h", .name = "Shirammy"
	},
	{
		.vmax = 72.0f, .walk = 12.0f, .accel = 28.0f, .coast = 26.0f,
		.turn_low = 2.5f, .turn_high = 2.15f, .jump = 40.0f, .climb = 50.0f,
		.hud_max = EXO_FLIGHT_HUD_REX, .hud_unit = "Mm/h", .name = "Rexxi"
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
	f->focal = EXO_FLIGHT_FOCAL;
	f->render_r = EXO_FLIGHT_RENDER;
	f->draw_cap = 48 + EXO_FLIGHT_RENDER * EXO_FLIGHT_RENDER;
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
	float frac;

	if (dt <= 0.0f || dt > 0.05f)
		dt = 1.0f / 60.0f;

	f->pad_x = sx;
	f->pad_y = sy;
	f->draw_cap = 48 + f->render_r * f->render_r;
	if (f->draw_cap > 720)
		f->draw_cap = 720;

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

		/* HIGH: pad como aviao. Cima no pad (sy+) desce, baixo sobe. */
		{
			float want = (-sy) * s->climb;
			if (fabsf(sy) < 0.12f)
				want = f->grounded ? 0.0f : -12.0f;
			f->vy += (want - f->vy) * clampf(dt * 8.0f, 0.0f, 1.0f);
		}
	} else {
		float mx, mz, mag;

		f->yaw += turn * s->turn_low * dt;
		fwd_x = sinf(f->yaw);
		fwd_z = cosf(f->yaw);
		rgt_x =  cosf(f->yaw);
		rgt_z = -sinf(f->yaw);

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

	f->move_x = f->wish_x;
	f->move_z = f->wish_z;

	if (tap_b && (f->grounded || f->mode == EXO_FLIGHT_HIGH)) {
		f->vy = s->jump;
		f->grounded = 0;
	}

	if (f->mode == EXO_FLIGHT_LOW)
		f->vy -= EXO_FLIGHT_GRAV * dt;

	f->y += f->vy * dt;
	if (f->y <= 0.0f) {
		f->y = 0.0f;
		if (f->vy < 0.0f)
			f->vy = 0.0f;
		f->grounded = 1;
	} else {
		f->grounded = 0;
		if (f->y > EXO_FLIGHT_Y_MAX) {
			f->y = EXO_FLIGHT_Y_MAX;
			if (f->vy > 0.0f)
				f->vy = 0.0f;
		}
	}

	if (f->mode == EXO_FLIGHT_LOW && f->speed >= EXO_FLIGHT_HYST_ENTER)
		f->mode = EXO_FLIGHT_HIGH;
	else if (f->mode == EXO_FLIGHT_HIGH && f->speed <= EXO_FLIGHT_HYST_LEAVE)
		f->mode = EXO_FLIGHT_LOW;

	/* visual only: tunnel vision quando a fracao de vmax sobe */
	frac = (vmax > 1.0f) ? clampf(f->speed / vmax, 0.0f, 1.0f) : 0.0f;
	f->focal = EXO_FLIGHT_FOCAL + frac * frac * 70.0f;

	if (f->mode == EXO_FLIGHT_HIGH) {
		f->cam_h = 28.0f + f->y;
		f->cam_dist = 42.0f;
		f->horizon = 88.0f - clampf(f->y * 0.08f, 0.0f, 18.0f);
	} else {
		f->cam_h = 50.0f + f->y;
		f->cam_dist = 58.0f;
		f->horizon = 66.0f;
	}

	f->cam_x = f->x - sinf(f->yaw) * f->cam_dist;
	f->cam_z = f->z - cosf(f->yaw) * f->cam_dist;
	f->cell_x = (int)floorf(f->x / EXO_FLIGHT_CELL);
	f->cell_z = (int)floorf(f->z / EXO_FLIGHT_CELL);
}

float exo_flight_hud_max(const ExoFlight *f)
{
	return exo_pilot_stats(f->pilot)->hud_max;
}

float exo_flight_hud_speed(const ExoFlight *f)
{
	float vmax = exo_flight_vmax(f);
	float full = exo_flight_hud_max(f);

	if (vmax <= 0.0f)
		return 0.0f;
	return (f->speed / vmax) * full;
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
	float foc = (f->focal > 80.0f) ? f->focal : EXO_FLIGHT_FOCAL;
	float k = foc / lz;
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
	float lx[4], lz[4];
	float half = cell * 0.5f;
	float foc = (f->focal > 80.0f) ? f->focal : EXO_FLIGHT_FOCAL;
	int i, ahead = 0;
	float min_lz = 1e9f;

	to_cam(f, wx + half, wz + half, eye_x, &lx[0], &lz[0]);
	if (out_lz)
		*out_lz = lz[0];

	to_cam(f, wx,        wz,        eye_x, &lx[0], &lz[0]);
	to_cam(f, wx + cell, wz,        eye_x, &lx[1], &lz[1]);
	to_cam(f, wx + cell, wz + cell, eye_x, &lx[2], &lz[2]);
	to_cam(f, wx,        wz + cell, eye_x, &lx[3], &lz[3]);

	for (i = 0; i < 4; ++i) {
		float lim;
		if (lz[i] < min_lz)
			min_lz = lz[i];
		if (lz[i] < EXO_FLIGHT_NEAR * 0.15f)
			continue;
		if (lz[i] > far_z + cell)
			continue;
		lim = lz[i] * (220.0f / foc) + cell * 1.35f;
		if (lx[i] <= lim && lx[i] >= -lim)
			ahead = 1;
	}
	if (out_lz && min_lz < 1e8f)
		*out_lz = (min_lz > 0.0f) ? min_lz : *out_lz;
	return ahead;
}

int exo_flight_clip_quad(const ExoFlight *f, float wx, float wz, float cell,
                         float eye_x, float sx[6], float sy[6], int *nv)
{
	float lx[4], lz[4];
	float olx[8], olz[8];
	int i, n = 0;
	float wxv[4], wzv[4];
	float pad = 0.55f;

	wxv[0] = wx - pad;        wzv[0] = wz - pad;
	wxv[1] = wx + cell + pad; wzv[1] = wz - pad;
	wxv[2] = wx + cell + pad; wzv[2] = wz + cell + pad;
	wxv[3] = wx - pad;        wzv[3] = wz + cell + pad;

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

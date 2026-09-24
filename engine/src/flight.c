#include "exo/flight.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static const ExoPilotStats STATS[2] = {
	{
		.vmax = 380.0f, .walk = 36.0f, .accel = 70.0f, .coast = 24.0f,
		.turn_low = 2.2f, .turn_high = 1.65f, .jump = 36.0f, .climb = 58.0f,
		.hud_max = EXO_FLIGHT_HUD_SHI, .hud_unit = "Gm/h", .name = "Shirammy"
	},
	{
		.vmax = 200.0f, .walk = 24.0f, .accel = 32.0f, .coast = 36.0f,
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
	return exo_pilot_stats(f->pilot)->vmax;
}

static void add_tree_cell(ExoFlight *f, int cx, int cz, float h)
{
	int i;
	if (f->tree_n >= EXO_FLIGHT_TREE_MAX)
		return;
	if (cx <= 0 || cz <= 0 || cx >= (int)f->map.w - 1 || cz >= (int)f->map.h - 1)
		return;
	for (i = 0; i < f->tree_n; ++i)
		if (f->trees[i].cx == cx && f->trees[i].cz == cz)
			return;
	f->trees[f->tree_n].cx = cx;
	f->trees[f->tree_n].cz = cz;
	f->trees[f->tree_n].h = h;
	f->tree_n++;
}

static void place_demo_trees(ExoFlight *f)
{
	int mid = EXO_TILEMAP_MAX / 2;
	static const int P[][2] = {
		{ 0, -3 }, { -2, 1 }, { 2, 4 }, { 0, 8 },
		{ -3, 12 }, { 3, 15 }, { 0, 11 }, { -2, 16 }
	};
	int i;
	f->tree_n = 0;
	for (i = 0; i < 8; ++i)
		add_tree_cell(f, mid + P[i][0], mid + P[i][1], 28.0f + (float)(i % 4) * 4.0f);
}

static unsigned urand(unsigned *s)
{
	*s = *s * 1664525u + 1013904223u;
	return *s;
}

static void setup_ss4_goals(ExoFlight *f)
{
	unsigned rng = 0xC0FFEEu ^ (unsigned)f->map.tile_count;
	int tries, i, cx, cz, mid;
	int chosen[3][2];
	int n = 0;

	mid = (int)f->map.w / 2;
	f->fake_n = 0;
	for (tries = 0; tries < 400 && n < 3; ++tries) {
		cx = 8 + (int)(urand(&rng) % (unsigned)(f->map.w - 16));
		cz = 8 + (int)(urand(&rng) % (unsigned)(f->map.h - 16));
		if (exo_tilemap_solid(&f->map, cx, cz))
			continue;
		if (abs(cx - mid) < 3 && abs(cz - mid) < 3)
			continue;
		chosen[n][0] = cx;
		chosen[n][1] = cz;
		n++;
	}
	if (n < 3) {
		chosen[0][0] = mid + 10; chosen[0][1] = mid + 16;
		chosen[1][0] = mid - 14; chosen[1][1] = mid + 8;
		chosen[2][0] = mid + 6;  chosen[2][1] = mid - 18;
		n = 3;
	}
	f->goal_cx = chosen[0][0];
	f->goal_cz = chosen[0][1];
	f->map.cells[f->goal_cz * f->map.w + f->goal_cx] = EXO_TILE_PAD;
	f->fake_n = 2;
	for (i = 1; i < 3; ++i) {
		f->fake_cx[i - 1] = chosen[i][0];
		f->fake_cz[i - 1] = chosen[i][1];
		f->map.cells[chosen[i][1] * f->map.w + chosen[i][0]] = EXO_TILE_PAD;
	}
}

static void setup_dp1_trees(ExoFlight *f)
{
	int x, z;
	f->tree_n = 0;
	for (z = 4; z < (int)f->map.h - 4 && f->tree_n < EXO_FLIGHT_TREE_MAX; z += 5) {
		for (x = 4; x < (int)f->map.w - 4 && f->tree_n < EXO_FLIGHT_TREE_MAX; x += 5) {
			uint16_t t = exo_tilemap_at(&f->map, x, z);
			if (t >= 8 || t == 1)
				add_tree_cell(f, x, z, 30.0f + (float)((x + z) % 5) * 3.0f);
		}
	}
}

void exo_flight_reset_run(ExoFlight *f)
{
	float mx = (float)f->map.w * 0.5f;
	float mz = (float)f->map.h * 0.5f;
	f->run = EXO_RUN_WAIT;
	f->run_t = 0.0f;
	f->laps = 0;
	f->lap_chk = 0;
	f->x = mx * EXO_FLIGHT_CELL;
	f->z = (mz - 8.0f) * EXO_FLIGHT_CELL;
	f->y = 0.0f;
	f->vy = 0.0f;
	f->speed = 0.0f;
	f->cruise = 0.0f;
	f->yaw = 0.0f;
	f->pitch = 0.0f;
	f->mode = EXO_FLIGHT_LOW;
	f->flying = 0;
	f->grounded = 1;
	f->charge = 0.0f;
	f->charge_armed = 0;
}

static void finish_course_setup(ExoFlight *f)
{
	if (f->course == EXO_COURSE_SS4)
		setup_ss4_goals(f);
	else if (f->course == EXO_COURSE_DP1)
		setup_dp1_trees(f);
	else {
		place_demo_trees(f);
		f->goal_cx = (int)f->map.w / 2;
		f->goal_cz = (int)f->map.h / 2 + 18;
	}
	exo_flight_reset_run(f);
}

int exo_flight_load_map(ExoFlight *f, ExoCourse course, const void *etm, uint32_t size)
{
	f->course = course;
	f->tree_n = 0;
	f->fake_n = 0;
	if (etm && size && exo_tilemap_load(&f->map, etm, size)) {
		finish_course_setup(f);
		return 1;
	}
	f->course = EXO_COURSE_DEMO;
	exo_tilemap_demo(&f->map);
	finish_course_setup(f);
	return 0;
}

void exo_flight_init(ExoFlight *f, ExoPilot pilot)
{
	memset(f, 0, sizeof(*f));
	f->pilot = pilot;
	f->cam_ref = EXO_CAM_SURFACE;
	f->focal = EXO_FLIGHT_FOCAL;
	f->render_r = EXO_FLIGHT_RENDER;
	f->draw_cap = 48 + EXO_FLIGHT_RENDER * EXO_FLIGHT_RENDER;
	exo_flight_load_map(f, EXO_COURSE_DEMO, NULL, 0);
}

void exo_flight_set_pilot(ExoFlight *f, ExoPilot pilot)
{
	f->pilot = pilot;
	if (f->speed > exo_flight_vmax(f))
		f->speed = exo_flight_vmax(f);
	if (f->cruise > exo_flight_vmax(f))
		f->cruise = exo_flight_vmax(f);
}

static bool blocked(const ExoFlight *f, float x, float z)
{
	int cx = (int)floorf(x / EXO_FLIGHT_CELL);
	int cz = (int)floorf(z / EXO_FLIGHT_CELL);
	int i;

	if (exo_tilemap_solid(&f->map, cx, cz))
		return true;
	for (i = 0; i < f->tree_n; ++i) {
		float tx = ((float)f->trees[i].cx + 0.5f) * EXO_FLIGHT_CELL;
		float tz = ((float)f->trees[i].cz + 0.5f) * EXO_FLIGHT_CELL;
		float dx = x - tx;
		float dz = z - tz;
		if (dx * dx + dz * dz < 36.0f && f->y < f->trees[i].h - 2.0f)
			return true;
	}
	return false;
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
	int hold_y = in && (in->held & EXO_BTN_Y);
	int tap_b = in && (in->down & EXO_BTN_B);
	int tap_zr = in && (in->down & EXO_BTN_ZR);
	int hold_l = in && (in->held & EXO_BTN_L);
	int hold_r = in && (in->held & EXO_BTN_R);
	float nx, nz, step, cap;
	float vmax = exo_flight_vmax(f);
	float fwd_x, fwd_z, rgt_x, rgt_z;
	float strafe;
	int stable;

	if (dt <= 0.0f || dt > 0.05f)
		dt = 1.0f / 60.0f;

	f->pad_x = sx;
	f->pad_y = sy;
	f->draw_cap = 48 + f->render_r * f->render_r;
	if (f->draw_cap > 720)
		f->draw_cap = 720;
	if (f->spring_cd > 0.0f) {
		f->spring_cd -= dt;
		if (f->spring_cd < 0.0f)
			f->spring_cd = 0.0f;
	}

	if (tap_zr)
		f->cam_ref = (f->cam_ref == EXO_CAM_SURFACE) ? EXO_CAM_PILOT : EXO_CAM_SURFACE;

	if (hold_y)
		f->charge += dt / EXO_FLIGHT_CHARGE_SEC;
	else
		f->charge -= dt / EXO_FLIGHT_CHARGE_DECAY;
	f->charge = clampf(f->charge, 0.0f, 1.0f);
	if (f->charge >= 1.0f)
		f->charge_armed = 1;
	else if (f->charge <= 0.0f)
		f->charge_armed = 0;
	stable = f->charge_armed && f->charge > 0.0f;

	if (f->mode == EXO_FLIGHT_LOW && f->speed >= EXO_FLIGHT_HYST_ENTER)
		f->mode = EXO_FLIGHT_HIGH;
	else if (f->mode == EXO_FLIGHT_HIGH && f->speed <= EXO_FLIGHT_HYST_LEAVE)
		f->mode = EXO_FLIGHT_LOW;

	f->flying = (f->mode == EXO_FLIGHT_HIGH && !f->grounded);
	strafe = (hold_r ? 1.0f : 0.0f) - (hold_l ? 1.0f : 0.0f);

	if (f->mode == EXO_FLIGHT_HIGH || f->flying) {
		f->yaw += sx * s->turn_high * dt;
		fwd_x = sinf(f->yaw);
		fwd_z = cosf(f->yaw);
		rgt_x =  cosf(f->yaw);
		rgt_z = -sinf(f->yaw);

		if (f->flying) {
			if (fabsf(sy) > 0.12f) {
				float want_p = (-sy) * EXO_FLIGHT_PITCH_MAX;
				f->pitch += (want_p - f->pitch) * clampf(dt * 6.0f, 0.0f, 1.0f);
			} else if (!stable) {
				f->pitch += (0.0f - f->pitch) * clampf(dt * 3.0f, 0.0f, 1.0f);
			}
		} else if (!stable) {
			f->pitch += (0.0f - f->pitch) * clampf(dt * 5.0f, 0.0f, 1.0f);
		}

		if (stable) {
			if (hold_y)
				f->cruise += s->accel * dt;
			else if (hold_a)
				f->cruise -= s->coast * 1.6f * dt;
			f->cruise = clampf(f->cruise, 0.0f, vmax);
			f->speed = f->cruise;
		} else {
			if (hold_y && f->speed < vmax)
				f->speed += s->accel * dt;
			else if (hold_a)
				f->speed -= s->coast * 1.8f * dt;
			else
				f->speed -= s->coast * dt;
			f->speed = clampf(f->speed, 0.0f, vmax);
			f->cruise = f->speed;
		}

		step = f->speed * cosf(f->pitch) * dt;
		if (step > EXO_FLIGHT_CELL * 0.45f)
			step = EXO_FLIGHT_CELL * 0.45f;
		nx = f->x + fwd_x * step;
		nz = f->z + fwd_z * step;
		if (!f->flying && fabsf(strafe) > 0.1f) {
			nx += rgt_x * strafe * s->walk * dt;
			nz += rgt_z * strafe * s->walk * dt;
		}
		f->wish_x = fwd_x;
		f->wish_z = fwd_z;
	} else {
		float mx, mz, mag;
		f->yaw += sx * s->turn_low * dt;
		if (!stable)
			f->pitch += (0.0f - f->pitch) * clampf(dt * 5.0f, 0.0f, 1.0f);
		fwd_x = sinf(f->yaw);
		fwd_z = cosf(f->yaw);
		rgt_x =  cosf(f->yaw);
		rgt_z = -sinf(f->yaw);

		mx = rgt_x * strafe + fwd_x * sy;
		mz = rgt_z * strafe + fwd_z * sy;
		mag = sqrtf(mx * mx + mz * mz);
		if (mag > 1.0f) {
			mx /= mag;
			mz /= mag;
			mag = 1.0f;
		}
		f->wish_x = mx;
		f->wish_z = mz;

		if (stable) {
			if (hold_y)
				f->cruise += s->accel * dt;
			else if (hold_a)
				f->cruise -= s->coast * 1.6f * dt;
			f->cruise = clampf(f->cruise, 0.0f, vmax);
			f->speed = f->cruise;
		} else {
			cap = hold_y ? vmax : s->walk;
			if (mag > 0.05f) {
				float target = cap * mag;
				if (f->speed < target)
					f->speed += s->accel * dt;
				else
					f->speed -= s->coast * dt;
				f->speed = clampf(f->speed, 0.0f, target);
			} else {
				f->speed -= s->coast * dt;
				if (f->speed < 0.0f)
					f->speed = 0.0f;
			}
			f->cruise = f->speed;
		}

		step = f->speed * dt;
		if (step > EXO_FLIGHT_CELL * 0.45f)
			step = EXO_FLIGHT_CELL * 0.45f;
		if (mag > 0.05f || (stable && f->speed > 0.5f)) {
			float ux = (mag > 0.05f) ? mx : fwd_x;
			float uz = (mag > 0.05f) ? mz : fwd_z;
			nx = f->x + ux * step;
			nz = f->z + uz * step;
		} else {
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

	if (tap_b) {
		if (f->mode == EXO_FLIGHT_HIGH) {
			f->vy = s->jump * 1.25f;
			f->grounded = 0;
			f->flying = 1;
		} else if (f->grounded) {
			f->vy = s->jump;
			f->grounded = 0;
			f->flying = 0;
		}
	}

	if (f->grounded && f->spring_cd <= 0.0f &&
	    exo_tilemap_spring(&f->map, f->cell_x, f->cell_z)) {
		f->vy = EXO_FLIGHT_SPRING_VY;
		f->grounded = 0;
		f->spring_cd = 0.40f;
		if (f->mode == EXO_FLIGHT_HIGH)
			f->flying = 1;
	}

	if (f->flying) {
		float climb = f->speed * sinf(f->pitch);
		if (stable)
			f->vy += (climb - f->vy) * clampf(dt * 8.0f, 0.0f, 1.0f);
		else {
			f->vy += (climb - f->vy) * clampf(dt * 4.0f, 0.0f, 1.0f);
			f->vy -= EXO_FLIGHT_GRAV * dt;
		}
	} else {
		f->vy -= EXO_FLIGHT_GRAV * dt;
	}

	f->y += f->vy * dt;
	if (f->y <= 0.0f) {
		f->y = 0.0f;
		if (f->vy < 0.0f)
			f->vy = 0.0f;
		f->grounded = 1;
		if (!stable)
			f->pitch += (0.0f - f->pitch) * clampf(dt * 8.0f, 0.0f, 1.0f);
		if (f->mode != EXO_FLIGHT_HIGH)
			f->flying = 0;
	} else {
		f->grounded = 0;
		if (f->y > EXO_FLIGHT_Y_MAX) {
			f->y = EXO_FLIGHT_Y_MAX;
			if (f->vy > 0.0f)
				f->vy = 0.0f;
		}
	}

	f->flying = (f->mode == EXO_FLIGHT_HIGH && !f->grounded);

	{
		float want_bank = 0.0f;
		if (f->mode == EXO_FLIGHT_HIGH || f->flying)
			want_bank = sx * EXO_FLIGHT_BANK_MAX;
		f->bank += (want_bank - f->bank) * clampf(dt * 7.0f, 0.0f, 1.0f);
	}

	f->focal = EXO_FLIGHT_FOCAL;
	{
		float pin, eye, dist;
		if (f->mode == EXO_FLIGHT_HIGH || f->flying) {
			pin = EXO_FLIGHT_PIN_FLY;
			eye = 24.0f;
			dist = 48.0f;
		} else {
			pin = EXO_FLIGHT_PIN_LOW;
			eye = 42.0f;
			dist = 58.0f;
		}
		f->cam_dist = dist;
		f->cam_h = f->y + eye;
		f->horizon = pin - eye * (EXO_FLIGHT_FOCAL / dist);
	}

	f->cam_x = f->x - sinf(f->yaw) * f->cam_dist;
	f->cam_z = f->z - cosf(f->yaw) * f->cam_dist;
	f->cell_x = (int)floorf(f->x / EXO_FLIGHT_CELL);
	f->cell_z = (int)floorf(f->z / EXO_FLIGHT_CELL);

	if (f->run == EXO_RUN_WAIT && f->speed > 4.0f)
		f->run = EXO_RUN_GO;
	if (f->run == EXO_RUN_GO) {
		f->run_t += dt;
		if (f->course == EXO_COURSE_DP1) {
			int midz = (int)f->map.h / 2;
			if (!f->lap_chk && f->cell_z > midz + 10)
				f->lap_chk = 1;
			if (f->lap_chk && f->cell_z < midz - 6 && f->grounded) {
				f->laps++;
				f->lap_chk = 0;
				if (f->laps >= 3) {
					f->run = EXO_RUN_DONE;
					if (f->best_t <= 0.0f || f->run_t < f->best_t)
						f->best_t = f->run_t;
				}
			}
		} else if (f->grounded && f->y < 1.5f &&
		           f->cell_x == f->goal_cx && f->cell_z == f->goal_cz) {
			f->run = EXO_RUN_DONE;
			if (f->best_t <= 0.0f || f->run_t < f->best_t)
				f->best_t = f->run_t;
		}
	}
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

float exo_flight_speed_frac(const ExoFlight *f)
{
	float vmax = exo_flight_vmax(f);
	if (vmax <= 0.0f)
		return 0.0f;
	return clampf(f->speed / vmax, 0.0f, 1.0f);
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

static void cam_to_screen(const ExoFlight *f, float lx, float lz, float wy,
                          float *sx, float *sy)
{
	float ly = f->cam_h - wy;
	float k;

	if (f->cam_ref == EXO_CAM_PILOT) {
		float cp = cosf(-f->pitch);
		float sp = sinf(-f->pitch);
		float ly2 = ly * cp - lz * sp;
		float lz2 = ly * sp + lz * cp;
		ly = ly2;
		lz = lz2;
	}
	if (lz < EXO_FLIGHT_NEAR)
		lz = EXO_FLIGHT_NEAR;
	k = EXO_FLIGHT_FOCAL / lz;
	*sx = 200.0f + lx * k;
	*sy = f->horizon + ly * k;
}

void exo_flight_apply_ref(const ExoFlight *f, float *sx, float *sy)
{
	float cx = 200.0f;
	float cy = f->horizon;
	float b, c, s, dx, dy;

	if (f->cam_ref != EXO_CAM_PILOT)
		return;
	if (fabsf(f->bank) < 0.002f)
		return;
	b = f->bank;
	c = cosf(b);
	s = sinf(b);
	dx = *sx - cx;
	dy = *sy - cy;
	*sx = cx + dx * c - dy * s;
	*sy = cy + dx * s + dy * c;
}

int exo_flight_project3(const ExoFlight *f, float wx, float wy, float wz,
                        float eye_x, float *sx, float *sy)
{
	float lx, lz;
	to_cam(f, wx, wz, eye_x, &lx, &lz);
	if (lz < EXO_FLIGHT_NEAR * 0.5f)
		return 0;
	cam_to_screen(f, lx, lz, wy, sx, sy);
	exo_flight_apply_ref(f, sx, sy);
	return 1;
}

int exo_flight_project(const ExoFlight *f, float wx, float wz,
                       float eye_x, float *sx, float *sy)
{
	return exo_flight_project3(f, wx, f->y, wz, eye_x, sx, sy);
}

int exo_flight_tile_visible(const ExoFlight *f, float wx, float wz,
                            float cell, float eye_x, float far_z, float *out_lz)
{
	float lx[4], lz[4];
	float half = cell * 0.5f;
	float foc = EXO_FLIGHT_FOCAL;
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
	for (i = 0; i < n; ++i) {
		cam_to_screen(f, olx[i], olz[i], 0.0f, &sx[i], &sy[i]);
		exo_flight_apply_ref(f, &sx[i], &sy[i]);
	}
	*nv = n;
	return 1;
}

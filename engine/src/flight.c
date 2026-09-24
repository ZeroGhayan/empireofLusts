#include "exo/flight.h"
#include "exo/terrain.h"
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

static ExoTerrain cell_ter(const ExoFlight *f, int cx, int cz)
{
	return exo_terrain_kind(f->course, exo_tilemap_at(&f->map, cx, cz));
}

static void add_tree_cell(ExoFlight *f, int cx, int cz, float ox, float oz, float h)
{
	int i;
	if (f->tree_n >= EXO_FLIGHT_TREE_MAX)
		return;
	if (cx < 0 || cz < 0 || cx >= (int)f->map.w || cz >= (int)f->map.h)
		return;
	for (i = 0; i < f->tree_n; ++i)
		if (f->trees[i].cx == cx && f->trees[i].cz == cz &&
		    fabsf(f->trees[i].ox - ox) < 0.05f && fabsf(f->trees[i].oz - oz) < 0.05f)
			return;
	f->trees[f->tree_n].cx = cx;
	f->trees[f->tree_n].cz = cz;
	f->trees[f->tree_n].ox = ox;
	f->trees[f->tree_n].oz = oz;
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
		add_tree_cell(f, mid + P[i][0], mid + P[i][1], 0.5f, 0.5f,
		              28.0f + (float)(i % 4) * 4.0f);
}

static unsigned urand(unsigned *s)
{
	*s = *s * 1664525u + 1013904223u;
	return *s;
}

static int ss4_pad_ok(const ExoFlight *f, int cx, int cz)
{
	ExoTerrain t;
	if (cx < 2 || cz < 2 || cx >= (int)f->map.w - 2 || cz >= (int)f->map.h - 2)
		return 0;
	t = cell_ter(f, cx, cz);
	if (t != EXO_TER_NONE)
		return 0;
	return 1;
}

static void spawn_ss4_pad(ExoFlight *f)
{
	unsigned rng = 0xA5A5u ^ (unsigned)f->score * 7919u ^ (unsigned)f->map.tile_count;
	int tries, cx, cz;

	for (tries = 0; tries < 500; ++tries) {
		cx = 4 + (int)(urand(&rng) % (unsigned)(f->map.w - 8));
		cz = 4 + (int)(urand(&rng) % (unsigned)(f->map.h - 8));
		if (!ss4_pad_ok(f, cx, cz))
			continue;
		if (cx == f->goal_cx && cz == f->goal_cz)
			continue;
		f->goal_cx = cx;
		f->goal_cz = cz;
		return;
	}
	f->goal_cx = (int)f->map.w / 2;
	f->goal_cz = (int)f->map.h / 2 + 12;
}

static void setup_ss4_goals(ExoFlight *f)
{
	f->fake_n = 0;
	f->score = 0;
	f->goal_cx = -1;
	f->goal_cz = -1;
	spawn_ss4_pad(f);
}

static void setup_dp1_bushes(ExoFlight *f)
{
	int x, z;

	f->tree_n = 0;
	for (z = 0; z < (int)f->map.h - 1; ++z) {
		for (x = 0; x < (int)f->map.w - 1; ++x) {
			uint16_t tl = exo_tilemap_at(&f->map, x, z);
			uint16_t tr = exo_tilemap_at(&f->map, x + 1, z);
			uint16_t bl = exo_tilemap_at(&f->map, x, z + 1);
			uint16_t br = exo_tilemap_at(&f->map, x + 1, z + 1);
			if (exo_terrain_is_bush_tl(f->course, tl, tr, bl, br))
				add_tree_cell(f, x, z, 1.0f, 1.0f, 34.0f);
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
	f->score = 0;
	f->water_t = 0.0f;
	f->submerged = 0;
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
	if (f->course == EXO_COURSE_SS4)
		spawn_ss4_pad(f);
}

static void finish_course_setup(ExoFlight *f)
{
	f->goal_cx = -1;
	f->goal_cz = -1;
	if (f->course == EXO_COURSE_SS4)
		setup_ss4_goals(f);
	else if (f->course == EXO_COURSE_DP1)
		setup_dp1_bushes(f);
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

int exo_flight_is_goal(const ExoFlight *f, int cx, int cz)
{
	return f->course == EXO_COURSE_SS4 && cx == f->goal_cx && cz == f->goal_cz;
}

static bool blocked(const ExoFlight *f, float x, float z)
{
	int cx = (int)floorf(x / EXO_FLIGHT_CELL);
	int cz = (int)floorf(z / EXO_FLIGHT_CELL);
	int i;
	ExoTerrain t = cell_ter(f, cx, cz);

	if (t == EXO_TER_WALL && f->y < EXO_FLIGHT_WALL_H)
		return true;
	for (i = 0; i < f->tree_n; ++i) {
		float tx = ((float)f->trees[i].cx + f->trees[i].ox) * EXO_FLIGHT_CELL;
		float tz = ((float)f->trees[i].cz + f->trees[i].oz) * EXO_FLIGHT_CELL;
		float dx = x - tx;
		float dz = z - tz;
		if (dx * dx + dz * dz < 64.0f && f->y < f->trees[i].h - 2.0f)
			return true;
	}
	return false;
}

static int bumper_ahead(const ExoFlight *f, float x, float z)
{
	int cx = (int)floorf(x / EXO_FLIGHT_CELL);
	int cz = (int)floorf(z / EXO_FLIGHT_CELL);
	return cell_ter(f, cx, cz) == EXO_TER_BUMPER;
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
	ExoTerrain here;

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

	if (bumper_ahead(f, nx, f->z) || bumper_ahead(f, f->x, nz)) {
		f->yaw += 3.14159265f;
		f->speed *= 0.85f;
		nx = f->x - sinf(f->yaw) * 10.0f;
		nz = f->z - cosf(f->yaw) * 10.0f;
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

	f->cell_x = (int)floorf(f->x / EXO_FLIGHT_CELL);
	f->cell_z = (int)floorf(f->z / EXO_FLIGHT_CELL);
	here = cell_ter(f, f->cell_x, f->cell_z);

	if (f->grounded && here == EXO_TER_BOOST_N)
		f->z -= EXO_FLIGHT_BOOST * dt;
	else if (f->grounded && here == EXO_TER_BOOST_S)
		f->z += EXO_FLIGHT_BOOST * dt;
	else if (f->grounded && here == EXO_TER_BOOST_W)
		f->x -= EXO_FLIGHT_BOOST * dt;
	else if (f->grounded && here == EXO_TER_BOOST_E)
		f->x += EXO_FLIGHT_BOOST * dt;

	if (tap_b && !f->submerged) {
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
	    (here == EXO_TER_SPRING || exo_tilemap_spring(&f->map, f->cell_x, f->cell_z))) {
		f->vy = EXO_FLIGHT_SPRING_VY;
		f->grounded = 0;
		f->spring_cd = 0.40f;
		if (f->mode == EXO_FLIGHT_HIGH)
			f->flying = 1;
	}

	if (here == EXO_TER_TRAP) {
		if (f->flying || f->y > 2.0f)
			f->vy -= 160.0f * dt;
		else if (f->speed > 8.0f) {
			f->speed = 0.0f;
			f->cruise = 0.0f;
			f->vy = -20.0f;
			f->y = 0.0f;
			f->grounded = 1;
			f->flying = 0;
			f->mode = EXO_FLIGHT_LOW;
		}
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

	{
		int wet = (here == EXO_TER_WATER);
		int dive = wet && (f->mode == EXO_FLIGHT_LOW || f->grounded || f->submerged || f->y < 1.0f);
		if (dive) {
			f->submerged = 1;
			f->flying = 0;
			f->grounded = 0;
			if (f->y > -2.0f)
				f->y = -2.0f;
			f->y -= 10.0f * dt;
			if (f->y < -18.0f)
				f->y = -18.0f;
			f->speed *= 0.92f;
			f->water_t += dt;
			if (f->water_t >= EXO_FLIGHT_WATER_DROWN)
				exo_flight_reset_run(f);
		} else {
			f->submerged = 0;
			f->water_t = 0.0f;
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
		}
	}

	f->flying = (f->mode == EXO_FLIGHT_HIGH && !f->grounded && !f->submerged);

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
	here = cell_ter(f, f->cell_x, f->cell_z);

	if (f->run == EXO_RUN_WAIT && f->speed > 4.0f)
		f->run = EXO_RUN_GO;
	if (f->run == EXO_RUN_GO) {
		float tscale = 1.0f;
		if (f->course == EXO_COURSE_SS4 && here == EXO_TER_WATER)
			tscale = 4.0f;
		f->run_t += dt * tscale;

		if (f->course == EXO_COURSE_DP1) {
			if (here != EXO_TER_FINISH)
				f->lap_chk = 1;
			if (f->lap_chk && here == EXO_TER_FINISH && f->grounded) {
				f->laps++;
				f->lap_chk = 0;
				if (f->laps >= 3) {
					f->run = EXO_RUN_DONE;
					if (f->best_t <= 0.0f || f->run_t < f->best_t)
						f->best_t = f->run_t;
				}
			}
		} else if (f->course == EXO_COURSE_SS4) {
			if (f->grounded && f->y < 1.5f &&
			    f->cell_x == f->goal_cx && f->cell_z == f->goal_cz) {
				f->score++;
				if (f->score >= EXO_FLIGHT_SS4_NEED) {
					f->run = EXO_RUN_DONE;
					if (f->best_t <= 0.0f || f->run_t < f->best_t)
						f->best_t = f->run_t;
				} else {
					spawn_ss4_pad(f);
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

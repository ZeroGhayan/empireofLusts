#include "exo/platform.h"
#include "exo/body.h"
#include "exo/combat.h"
#include "exo/elem.h"
#include "exo/profile.h"
#include <citro2d.h>
#include <stdio.h>

#define STAGE_W    640.0f
#define GROUND_Y   200.0f
#define GRAVITY    900.0f
#define WALK       160.0f
#define BACKWALK   112.0f
#define AIR        110.0f
#define JUMP_SPD   320.0f
#define BODY_W     36.0f
#define BODY_H     42.0f
#define KO_WAIT    90
#define MATCH_WAIT 90
#define FT_WINS    2
#define DASH_FR    10
#define DASH_SPD   320.0f
#define TAP_WIN    20
#define TAP_EDGE   0.40f
#define BUF_WIN    8

static const ExoMove PUNCH_BASE = {
	.startup  = 6,
	.active = 4,
	.recovery = 12,
	.box_x = 28.0f, .box_y = 4.0f, .box_w = 22.0f, .box_h = 12.0f,
	.kb_x = 140.0f, .kb_y = 80.0f,
	.hitstun = 14,
	.damage = 10,
	.air_ok = false,
	.type = EXO_FIRE,
	.height = EXO_HT_MID,
	.grab = false,
	.meter_hit = 20,
	.chip = 0
};

static const ExoMove AIR_BASE = {
	.startup  = 5,
	.active = 5,
	.recovery = 10,
	.box_x = 24.0f, .box_y = 18.0f, .box_w = 24.0f, .box_h = 18.0f,
	.kb_x = 110.0f, .kb_y = 40.0f,
	.hitstun = 12,
	.damage = 8,
	.air_ok = true,
	.type = EXO_FIRE,
	.height = EXO_HT_OVER,
	.grab = false,
	.meter_hit = 20,
	.chip = 0
};

static const ExoMove KICK_BASE = {
	.startup  = 9,
	.active = 5,
	.recovery = 16,
	.box_x = 34.0f, .box_y = 22.0f, .box_w = 30.0f, .box_h = 14.0f,
	.kb_x = 180.0f, .kb_y = 36.0f,
	.hitstun = 16,
	.damage = 14,
	.air_ok = false,
	.type = EXO_FIGHTING,
	.height = EXO_HT_LOW,
	.grab = false,
	.meter_hit = 20,
	.chip = 0
};

static const ExoMove GRAB_MOVE = {
	.startup  = 6,
	.active = 4,
	.recovery = 24,
	.box_x    = 20.0f, .box_y    = 6.0f, .box_w    = 28.0f, .box_h    = 30.0f,
	.kb_x     = 0.0f, .kb_y     = 0.0f,
	.hitstun  = 0,
	.damage   = 0,
	.air_ok   = false,
	.type     = EXO_NORMAL,
	.height   = EXO_HT_MID,
	.grab     = true,
	.meter_hit = 0,
	.chip = 0
};

static const ExoMove SPECIAL_BASE = {
	.startup  = 12,
	.active = 6,
	.recovery = 22,
	.box_x = 40.0f, .box_y = 8.0f, .box_w = 36.0f, .box_h = 20.0f,
	.kb_x = 200.0f, .kb_y = 120.0f,
	.hitstun = 20,
	.damage = 20,
	.air_ok = false,
	.type = EXO_FIRE,
	.height = EXO_HT_MID,
	.grab = false,
	.meter_hit = 0,
	.chip = 5
};

static ExoMove g_punch, g_air, g_kick, g_special;
static ExoElem g_atk = EXO_FIRE;
static uint8_t g_combo;
static uint8_t g_combo_disp;
static uint8_t g_combo_hold;
static uint8_t g_counter;
static uint8_t g_riposte;
static uint8_t g_meter;
static uint8_t buf_y, buf_x, buf_b;
static unsigned g_pid;
static const ExoProfile *g_p1p;
static C2D_SpriteSheet g_sheet;
static uint8_t g_idle_t;

typedef struct Match {
	uint8_t wins[2];
	uint8_t round;
	bool last_round;
	bool over;
} Match;

static Match g_match;

typedef struct Dash {
	uint8_t run;
	int8_t  dir;
	uint8_t tap;
	int8_t  tap_dir;
	int8_t  prev;
} Dash;

static uint8_t p_meter_max(void)
{
	return g_p1p ? g_p1p->meter_max : 100;
}

static uint8_t p_meter_cost(void)
{
	return g_p1p ? g_p1p->meter_cost : 50;
}

static void meter_add(uint8_t n)
{
	uint16_t cap = p_meter_max();
	uint16_t v = (uint16_t)g_meter + n;

	g_meter = (uint8_t)(v > cap ? cap : v);
}

static void combo_tick(void)
{
	if (g_combo >= 2) {
		g_combo_disp = g_combo;
		g_combo_hold = 48;
	} else if (g_combo_hold) {
		g_combo_hold--;
		if (!g_combo_hold)
			g_combo_disp = 0;
	}
}

static void set_atk(ExoElem e)
{
	g_atk = e;
	g_punch = PUNCH_BASE;
	g_air = AIR_BASE;
	g_kick = KICK_BASE;
	g_punch.type = e;
	g_air.type = e;
	g_kick.type = EXO_FIGHTING;
	g_combo = 0;
	g_special = SPECIAL_BASE;
	g_special.type = e;
}

static void match_init(void)
{
	g_match.wins[0] = 0;
	g_match.wins[1] = 0;
	g_match.round = 1;
	g_match.last_round = false;
	g_match.over = false;
}

static void match_award(const ExoFighter *p1, const ExoFighter *p2)
{
	if (p1->hp <= 0 && p2->hp <= 0)
		return;
	if (p2->hp <= 0)
		g_match.wins[0]++;
	else
		g_match.wins[1]++;

	if (g_match.wins[0] >= FT_WINS || g_match.wins[1] >= FT_WINS)
		g_match.over = true;
	else {
		g_match.round++;
		g_match.last_round =
		    (g_match.wins[0] == FT_WINS - 1 &&
		     g_match.wins[1] == FT_WINS - 1);
	}
}

static int8_t axis_dir(float x)
{
	if (x >  TAP_EDGE) return 1;
	if (x < -TAP_EDGE) return -1;
	return 0;
}

static void dash_start(Dash *d, int8_t dir)
{
	if (dir == 0)
		return;
	d->run = DASH_FR;
	d->dir = dir;
	d->tap = 0;
}

static void dash_poll(Dash *d, float axis, bool can, bool grounded)
{
	int8_t dir = axis_dir(axis);

	if (d->tap > 0)
		d->tap--;
	if (can && grounded && dir != 0 && d->prev == 0) {
		if (d->tap > 0 && d->tap_dir == dir)
			dash_start(d, dir);
		else {
			d->tap = TAP_WIN;
			d->tap_dir = dir;
		}
	}
	d->prev = dir;
}

static uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	return C2D_Color32(r, g, b, a);
}

static float clampf(float v, float lo, float hi)
{
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

static void control(ExoBody *b, float axis_x, bool jump, Dash *d)
{
	if (d->run > 0) {
		b->vx = (float)d->dir * DASH_SPD;
		d->run--;
		if (jump)
			exo_body_jump(b, JUMP_SPD);
	} else {
		float spd = b->grounded ? WALK : AIR;
		if (b->grounded && axis_x * (float)b->facing < 0.0f)
			spd = BACKWALK;
		b->vx = axis_x * spd;
		if (jump)
			exo_body_jump(b, JUMP_SPD);
	}
	b->x = clampf(b->x, 0.0f, STAGE_W - b->w);
}

static bool want_block(const ExoFighter *f, float axis_x, bool shoulder)
{
	if (shoulder)
		return true;
	if (f->body.facing > 0 && axis_x < -0.5f)
		return true;
	if (f->body.facing < 0 && axis_x >  0.5f)
		return true;
	return false;
}

static const ExoMove *pick_move(bool grounded, bool down)
{
	if (down && grounded)
		return &g_kick;
	return grounded ? &g_punch : &g_air;
}

static void snap_grab(ExoFighter *att, ExoFighter *vic)
{
	if (att->body.facing > 0)
		vic->body.x = att->body.x + att->body.w + 2.0f;
	else
		vic->body.x = att->body.x - vic->body.w - 2.0f;
	vic->body.y = att->body.y;
	vic->body.vx = 0.0f;
	vic->body.vy = 0.0f;
	att->body.vx = 0.0f;
}

static void do_throw(ExoFighter *att, ExoFighter *vic, int dir)
{
	float f = (float)att->body.facing;
	float back;

	att->phase = EXO_PHASE_RECOVERY;
	att->timer = 16;
	att->move = 0;
	vic->phase = EXO_PHASE_HITSTUN;
	vic->move = 0;
	vic->body.grounded = false;
	vic->hp = (int16_t)(vic->hp - 18);
	if (vic->hp < 0)
		vic->hp = 0;

	switch (dir) {
	case 2:
		back = -f;
		att->body.facing = (int8_t)back;
		if (back > 0.0f)
			vic->body.x = att->body.x + att->body.w + 4.0f;
		else
			vic->body.x = att->body.x - vic->body.w - 4.0f;
		vic->body.vx = back * 210.0f;
		vic->body.vy = -90.0f;
		vic->timer = 20;
		break;
	case 3:
		vic->body.vx = f * 30.0f;
		vic->body.vy = -300.0f;
		vic->timer = 26;
		break;
	case 4:
		vic->body.vx = f * 50.0f;
		vic->body.vy = -30.0f;
		vic->timer = 14;
		break;
	default:
		vic->body.vx = f * 230.0f;
		vic->body.vy = -110.0f;
		vic->timer = 20;
		break;
	}
}

static int read_throw_dir(const ExoFighter *att, const ExoInput *in)
{
	if (in->stick_y >  0.55f) return 3;
	if (in->stick_y < -0.55f) return 4;
	if (in->stick_x >  0.55f)
		return (att->body.facing > 0) ? 1 : 2;
	if (in->stick_x < -0.55f)
		return (att->body.facing > 0) ? 2 : 1;
	return 0;
}

static void resolve_hits(ExoFighter *att, ExoFighter *vic, uint8_t *hitstop)
{
	ExoRect hb, hurt;
	const ExoMove *m;
	bool can_block;

	if (att->hit_done || !exo_fighter_hitbox(att, &hb))
		return;
	hurt = exo_fighter_hurt(vic);
	if (!exo_aabb(&hb, &hurt))
		return;

	m = att->move;
	if (m && exo_elem_mul2(m->type, vic->type, vic->type2) == 0) {
		att->hit_done = true;
		return;
	}

	if (m && m->grab) {
		if (vic->body.grounded)
			exo_fighter_apply_grab(vic, att);
		g_combo = 0;
		att->hit_done = true;
		*hitstop = 3;
		return;
	}

	if (vic->phase == EXO_PHASE_TAUNT && vic->taunt_is_counter) {
		exo_fighter_taunt_riposte(vic, att);
		g_combo = 0;
		g_riposte = 16;
		att->hit_done = true;
		*hitstop = 6;
		return;
	}

	can_block = exo_fighter_blocking(vic) && exo_fighter_in_front(att, vic);
	if (can_block && m) {
		if (m->height == EXO_HT_LOW && !vic->crouched)
			can_block = false;
		if (m->height == EXO_HT_OVER && vic->crouched)
			can_block = false;
	}

	if (can_block) {
		exo_fighter_apply_block(vic, att);
		if (m && m->chip && vic->hp > 0) {
			int16_t c = (int16_t)m->chip;
			if (vic->hp > c)
				vic->hp = (int16_t)(vic->hp - c);
			else
				vic->hp = 1;
		}
		g_combo = 0;
		meter_add(4);
		*hitstop = 2;
	} else {
		bool ch = (vic->phase == EXO_PHASE_STARTUP ||
			vic->phase == EXO_PHASE_ACTIVE);
		exo_fighter_apply_hit(vic, att);
		att->cancel = true;
		g_combo++;
		if (m && m->meter_hit)
			meter_add(m->meter_hit);
		if (ch) {
			int16_t extra = 5;
			if (vic->hp > extra)
				vic->hp = (int16_t)(vic->hp - extra);
			else if (vic->hp > 0)
				vic->hp = 1;
			vic->timer = (uint8_t)(vic->timer + 8);
			g_counter = 12;
		}
		*hitstop = 4;
	}
	att->hit_done = true;
}

static int hit(uint16_t x, uint16_t y, int x0, int y0, int x1, int y1)
{
	return x >= (uint16_t)x0 && x < (uint16_t)x1 &&
	       y >= (uint16_t)y0 && y < (uint16_t)y1;
}

static void touch_hud(const ExoInput *in, ExoFighter *p1, ExoFighter *p2)
{
	uint16_t x, y;

	if (!in->touch_press)
		return;
	x = in->touch_x;
	y = in->touch_y;
	if (hit(x, y, 8, 30, 312, 52)) {
		p1->type = exo_elem_next(p1->type);
		p1->type2 = p1->type;
	} else if (hit(x, y, 8, 52, 160, 76)) {
		p2->type = exo_elem_next(p2->type);
	} else if (hit(x, y, 160, 52, 312, 76)) {
		p2->type2 = exo_elem_next(p2->type2);
	} else if (hit(x, y, 8, 80, 312, 128)) {
		set_atk(exo_elem_next(g_atk));
	}
}

static void draw_fighter(const ExoFighter *f, float cam, float par,
                         uint32_t body, uint32_t accent, bool use_spr)
{
	float h = f->crouched ? 22.0f : f->body.h;
	float sy = f->body.y + f->body.h - h;
	float sx = f->body.x - cam + par;

	if (use_spr && g_sheet) {
		C2D_Sprite spr;
		int fr = (g_idle_t / 20) & 1;

		C2D_SpriteFromSheet(&spr, g_sheet, fr);
		C2D_SpriteSetCenter(&spr, 0.5f, 1.0f);
		C2D_SpriteSetPos(&spr, sx + f->body.w * 0.5f, f->body.y + f->body.h);
		C2D_SpriteSetScale(&spr,
		                    (f->body.facing > 0) ? 1.0f : -1.0f,
		                    f->crouched ? 0.55f : 1.0f);
		C2D_DrawSprite(&spr);
	} else {
		C2D_DrawRectangle(sx, sy, 0.0f, f->body.w, h, body, body, body, body);
		{
			float eye_x = (f->body.facing > 0) ? (sx + f->body.w - 14.0f) : (sx + 4.0f);
			C2D_DrawRectangle(eye_x, sy + 4.0f, 0.0f, 10.0f, 6.0f,
			                  accent, accent, accent, accent);
		}
	}
	if (f->phase == EXO_PHASE_TAUNT) {
		uint32_t ylw = rgba(255, 220, 60, 255);
		C2D_DrawRectangle(sx + 6.0f, sy - 10.0f, 0.0f, f->body.w - 12.0f, 6.0f,
		                  ylw, ylw, ylw, ylw);
	}
}

static void draw_guard(const ExoFighter *f, float cam, ExoEye eye)
{
	float par, sx, x;
	uint32_t g;

	if (!exo_fighter_blocking(f))
		return;
	par = exo_parallax(12.0f, eye);
	sx = f->body.x - cam + par;
	x = (f->body.facing > 0) ? (sx + f->body.w) : (sx - 6.0f);
	g = rgba(200, 220, 255, 220);
	C2D_DrawRectangle(x, f->body.y + 8.0f, 0.0f, 6.0f, 24.0f, g, g, g, g);
}

static void draw_hitboxes(const ExoFighter *a, const ExoFighter *b, float cam, ExoEye eye)
{
	ExoRect d;
	float par = exo_parallax(10.0f, eye);
	uint32_t hx = rgba(255, 80, 40, 180);

	if (exo_fighter_hitbox(a, &d))
		C2D_DrawRectangle(d.x - cam + par, d.y, 0.0f, d.w, d.h, hx, hx, hx, hx);
	if (exo_fighter_hitbox(b, &d))
		C2D_DrawRectangle(d.x - cam + par, d.y, 0.0f, d.w, d.h, hx, hx, hx, hx);
}

static void pip(float x, float y, uint32_t c)
{
	C2D_DrawRectangle(x, y, 0.0f, 8.0f, 6.0f, c, c, c, c);
}

static void draw_hp_top(const ExoFighter *p1, const ExoFighter *p2, ExoEye eye)
{
	float near = exo_parallax(16.0f, eye);
	uint32_t back = rgba(20, 20, 24, 255);
	uint32_t b1   = rgba(70, 180, 255, 255);
	uint32_t b2   = rgba(230, 70, 90, 255);
	uint32_t gold = rgba(255, 200, 80, 255);
	float w1 = 120.0f * ((p1->hp_max > 0) ? (float)p1->hp / (float)p1->hp_max : 0.0f);
	float w2 = 120.0f * ((p2->hp_max > 0) ? (float)p2->hp / (float)p2->hp_max : 0.0f);
	int i;

	C2D_DrawRectangle(12.0f + near, 8.0f, 0.0f, 120.0f, 8.0f, back, back, back, back);
	C2D_DrawRectangle(12.0f + near, 8.0f, 0.0f, w1, 8.0f, b1, b1, b1, b1);
	C2D_DrawRectangle(268.0f - near, 8.0f, 0.0f, 120.0f, 8.0f, back, back, back, back);
	C2D_DrawRectangle(268.0f - near + (120.0f - w2), 8.0f, 0.0f, w2, 8.0f, b2, b2, b2, b2);

	for (i = 0; i < FT_WINS; i++)
		pip(12.0f + near + (float)(i * 10), 18.0f,
		    (i < (int)g_match.wins[0]) ? b1 : back);
	for (i = 0; i < FT_WINS; i++)
		pip(268.0f - near + 120.0f - 8.0f - (float)(i * 10), 18.0f,
		    (i < (int)g_match.wins[1]) ? b2 : back);

	if (g_match.last_round && !g_match.over)
		C2D_DrawRectangle(192.0f, 18.0f, 0.0f, 16.0f, 6.0f, gold, gold, gold, gold);
}

static void draw_combo_top(ExoEye eye)
{
	char line[24];
	float near = exo_parallax(18.0f, eye);
	uint32_t gold = rgba(255, 210, 70, 255);
	uint32_t red  = rgba(255, 70, 70, 255);

	if (g_combo_disp >= 2) {
		snprintf(line, sizeof(line), "%u HIT", (unsigned)g_combo_disp);
		exo_top_text(200.0f + near, 32.0f, 0.7f, gold, line);
	}
	if (g_riposte)
		exo_top_text(200.0f + near, 56.0f, 0.5f, red, "COUNTER");
	else if (g_counter)
		exo_top_text(200.0f + near, 56.0f, 0.5f, red, "CH");
	if (g_match.last_round && !g_match.over)
		exo_top_text(200.0f + near, 208.0f, 0.45f, gold, "ULTIMO");
}

static void draw_world(ExoEye eye, float cam, const ExoFighter *p1, const ExoFighter *p2)
{
	float bg  = exo_parallax(4.0f,  eye);
	float mid = exo_parallax(10.0f, eye);
	uint32_t sky    = rgba(18, 22, 40, 255);
	uint32_t far    = rgba(36, 48, 78, 255);
	uint32_t floorc = rgba(52, 40, 64, 255);
	uint32_t line   = rgba(220, 200, 90, 255);
	uint32_t c1     = rgba(70, 180, 255, 255);
	uint32_t c1a    = rgba(255, 255, 255, 255);
	uint32_t c2     = rgba(230, 70, 90, 255);
	uint32_t c2a    = rgba(255, 220, 80, 255);
	int i;

	C2D_DrawRectangle(0.0f, 0.0f, 0.0f, EXO_TOP_W, EXO_TOP_H, sky, sky, sky, sky);
	for (i = 0; i < 8; ++i) {
		float x = (float)(i * 80) - cam + bg;
		C2D_DrawRectangle(x, 50.0f, 0.0f, 24.0f, 80.0f, far, far, far, far);
	}
	C2D_DrawRectangle(0.0f, GROUND_Y, 0.0f, EXO_TOP_W, EXO_TOP_H - GROUND_Y,
	                  floorc, floorc, floorc, floorc);
	C2D_DrawRectangle(0.0f, GROUND_Y - 2.0f, 0.0f, EXO_TOP_W, 3.0f, line, line, line, line);
	draw_fighter(p1, cam, mid, c1, c1a, true);
	draw_fighter(p2, cam, mid, c2, c2a, false);
}

static void bar(float x, float y, float w, float h, float fill, uint32_t fg)
{
	exo_bot_rect(x, y, w, h, rgba(24, 24, 30, 255));
	if (fill > 0.0f)
		exo_bot_rect(x, y, w * fill, h, fg);
}

static void draw_bottom(const ExoFighter *p1, const ExoFighter *p2, const Dash *d1, bool paused)
{
	char line[64];
	uint16_t mp = exo_elem_mul2(g_atk, p2->type, p2->type2);
	uint16_t mk = exo_elem_mul2(EXO_FIGHTING, p2->type, p2->type2);
	uint32_t t = rgba(230, 230, 236, 255);
	uint32_t dim = rgba(140, 144, 160, 255);
	uint32_t acc = rgba(255, 200, 80, 255);
	float f1 = (p1->hp_max > 0) ? (float)p1->hp / (float)p1->hp_max : 0.0f;
	float f2 = (p2->hp_max > 0) ? (float)p2->hp / (float)p2->hp_max : 0.0f;
	float mmax = (float)p_meter_max();

	exo_text_begin();
	if (paused) {
		exo_text(90.0f, 80.0f, 0.7f, acc, "PAUSA");
		exo_text(40.0f, 130.0f, 0.45f, t, "START  CONTINUA");
		exo_text(40.0f, 170.0f, 0.45f, acc, "TOQUE AQUI  SAIR");
		return;
	}

	bar(12.0f, 16.0f, 140.0f, 14.0f, f1, rgba(70, 180, 255, 255));
	bar(168.0f, 16.0f, 140.0f, 14.0f, f2, rgba(230, 70, 90, 255));

	snprintf(line, sizeof(line), "P1 %s %s  %d/%d",
	         g_p1p ? g_p1p->name : "?",
	         p1->taunt_is_counter ? "CNT" : "POSE",
	         (int)p1->hp, (int)p1->hp_max);
	exo_text(12.0f, 36.0f, 0.45f, t, line);
	snprintf(line, sizeof(line), "P2 %s/%s  %d",
	         exo_elem_name(p2->type), exo_elem_name(p2->type2), (int)p2->hp);
	exo_text(12.0f, 56.0f, 0.45f, t, line);

	if (g_match.over) {
		snprintf(line, sizeof(line), "%s  %u-%u",
		         (g_match.wins[0] >= FT_WINS) ? "P1 VENCE" : "P2 VENCE",
		         (unsigned)g_match.wins[0], (unsigned)g_match.wins[1]);
		exo_text(12.0f, 76.0f, 0.5f, acc, line);
	} else if (g_match.last_round) {
		snprintf(line, sizeof(line), "ULTIMO  %u-%u",
		         (unsigned)g_match.wins[0], (unsigned)g_match.wins[1]);
		exo_text(12.0f, 76.0f, 0.5f, acc, line);
	} else {
		snprintf(line, sizeof(line), "FT2  %u-%u  R%u",
		         (unsigned)g_match.wins[0], (unsigned)g_match.wins[1],
		         (unsigned)g_match.round);
		exo_text(12.0f, 76.0f, 0.45f, t, line);
	}

	snprintf(line, sizeof(line), "SOCO %s  x%u.%02u",
	         exo_elem_name(g_atk), (unsigned)(mp / 100), (unsigned)(mp % 100));
	exo_text(12.0f, 96.0f, 0.45f, acc, line);
	snprintf(line, sizeof(line), "CHUTE FIGHTING  x%u.%02u",
	         (unsigned)(mk / 100), (unsigned)(mk % 100));
	exo_text(12.0f, 114.0f, 0.45f, t, line);

	if (p1->phase == EXO_PHASE_GRAB)
		exo_text(12.0f, 136.0f, 0.45f, acc, "CLINCH  PAD ARREMESSAR");
	else if (p1->phase == EXO_PHASE_TAUNT)
		exo_text(12.0f, 136.0f, 0.45f, acc, "TAUNT");
	else
		exo_text(12.0f, 136.0f, 0.45f, dim,
		         p2->crouched ? "P2 AGACHADO  LOW OK" : "P2 EM PE  OVER OK");

	snprintf(line, sizeof(line), "DASH P1 %s", d1->run ? ">>>" : "---");
	exo_text(12.0f, 156.0f, 0.45f, d1->run ? acc : dim, line);

	if (p1->taunt_cd) {
		snprintf(line, sizeof(line), "TAUNT CD %u", (unsigned)p1->taunt_cd);
		exo_text(168.0f, 176.0f, 0.4f, dim, line);
	}

	bar(12.0f, 220.0f, 296.0f, 8.0f,
	    (mmax > 0.0f) ? (float)g_meter / mmax : 0.0f,
	    g_meter >= p_meter_cost() ? acc : dim);

	exo_text(12.0f, 186.0f, 0.4f, dim, "ZL PERFIL  SELECT TAUNT");
	exo_text(12.0f, 202.0f, 0.4f, dim, g_sheet ? "SPRITE ROMFS  P2 D-PAD" : "SEM SPRITE  P2 D-PAD");
}

static void reset_round(ExoFighter *p1, ExoFighter *p2, Dash *d1)
{
	ExoElem a = p1->type, a2 = p1->type2;
	ExoElem b = p2->type, b2 = p2->type2;

	exo_fighter_init(p1, 140.0f, GROUND_Y - BODY_H, BODY_W, BODY_H);
	exo_fighter_init(p2, 420.0f, GROUND_Y - BODY_H, BODY_W, BODY_H);
	if (g_p1p) {
		p1->hp_max = g_p1p->hp;
		p1->hp = g_p1p->hp;
		p1->taunt_is_counter = g_p1p->taunt_is_counter;
	}
	p1->type = a;
	p1->type2 = a2;
	p2->type = b;
	p2->type2 = b2;
	d1->run = 0;

	g_meter = 0;
	g_counter = 0;
	g_riposte = 0;
	g_combo = 0;
	g_combo_disp = 0;
	g_combo_hold = 0;
	buf_y = buf_x = buf_b = 0;
}

static void buf_tick(void)
{
	if (exo_down(EXO_BTN_Y)) buf_y = BUF_WIN;
	else if (buf_y) buf_y--;

	if (exo_down(EXO_BTN_X)) buf_x = BUF_WIN;
	else if (buf_x) buf_x--;

	if (exo_down(EXO_BTN_B)) buf_b = BUF_WIN;
	else if (buf_b) buf_b--;
}

int main(int argc, char **argv)
{
	uint32_t clear, bot;
	const ExoInput *in;
	uint8_t hitstop = 0, ko_timer = 0;
	ExoFighter p1, p2;
	Dash d1, d2;
	int8_t cprev = 0;
	bool paused = false;

	(void)argc;
	(void)argv;
	if (!exo_init())
		return 1;

	set_atk(EXO_FIRE);
	match_init();
	g_pid = 0;
	g_p1p = exo_profile(g_pid);
	exo_fighter_init(&p1, 140.0f, GROUND_Y - BODY_H, BODY_W, BODY_H);
	exo_fighter_init(&p2, 420.0f, GROUND_Y - BODY_H, BODY_W, BODY_H);
	exo_fighter_apply_profile(&p1, g_p1p);
	p2.type = p2.type2 = EXO_WATER;
	d1.run = d1.tap = 0;
	d1.dir = d1.tap_dir = d1.prev = 0;
	d2 = d1;
	clear = rgba(8, 8, 12, 255);
	bot = rgba(20, 32, 56, 255);
	g_sheet = C2D_SpriteSheetLoad("romfs:/gfx/p1.t3x");

	while (exo_frame_begin()) {
		float dt, a2, mid, cam;
		bool sh, ko, frozen, act1, grabbing;
		int td;
		int8_t cd;

		in = exo_input();
		if (exo_down(EXO_BTN_START))
			paused = !paused;

		if (paused) {
			if (in->touch_press &&
			    hit(in->touch_x, in->touch_y, 30, 150, 290, 210))
				break;
		} else {
			if (exo_down(EXO_BTN_ZL)) {
				g_pid++;
				g_p1p = exo_profile(g_pid);
				exo_fighter_apply_profile(&p1, g_p1p);
				if (g_meter > p_meter_max())
					g_meter = p_meter_max();
			}
			touch_hud(in, &p1, &p2);
			buf_tick();
			dt = exo_dt();
			frozen = (hitstop > 0);
			ko = (p1.hp <= 0 || p2.hp <= 0);
			act1 = exo_fighter_can_act(&p1);
			grabbing = (p1.phase == EXO_PHASE_GRAB);
			if (g_counter)
				g_counter--;
			if (g_riposte)
				g_riposte--;
			combo_tick();
			g_idle_t++;

			a2 = 0.0f;
			if (exo_held(EXO_BTN_LEFT))  a2 -= 1.0f;
			if (exo_held(EXO_BTN_RIGHT)) a2 += 1.0f;
			sh = exo_held(EXO_BTN_L) || exo_held(EXO_BTN_R);

			exo_fighter_crouch(&p1, in->stick_y < -0.50f);
			exo_fighter_crouch(&p2, exo_held(EXO_BTN_DOWN));

			dash_poll(&d1, in->stick_x, act1 && !frozen && !ko && !grabbing, p1.body.grounded);
			cd = axis_dir(in->cstick_x);
			if (act1 && !frozen && !ko && !grabbing && p1.body.grounded && cd != 0 && cprev == 0)
				dash_start(&d1, cd);
			cprev = cd;

			if (frozen) {
				hitstop--;
			} else if (!ko) {
				if (grabbing) {
					snap_grab(&p1, &p2);
					td = read_throw_dir(&p1, in);
					if (td != 0 || p1.timer == 0)
						do_throw(&p1, &p2, td ? td : 1);
				} else if (act1 || p1.phase == EXO_PHASE_BLOCK || p1.cancel) {
					if (act1 || p1.phase == EXO_PHASE_BLOCK)
						exo_fighter_guard(&p1, want_block(&p1, in->stick_x, false));
					if (act1) {
						bool j1 = (exo_down(EXO_BTN_A) ||
						           (in->stick_y > 0.65f && p1.body.grounded))
						          && !(in->stick_y < -0.50f);
						control(&p1.body, in->stick_x, j1, &d1);
						if (exo_down(EXO_BTN_SELECT) && g_p1p)
							exo_fighter_taunt(&p1, g_p1p->taunt_frames,
							                  g_p1p->taunt_cd);
					} else {
						p1.body.vx = 0.0f;
						d1.run = 0;
					}
					if (buf_b && p1.body.grounded && act1) {
						if (exo_fighter_attack(&p1, &GRAB_MOVE)) {
							buf_b = 0;
							d1.run = 0;
						}
					} else if (buf_y) {
						if (exo_fighter_attack(&p1,
						    pick_move(p1.body.grounded, in->stick_y < -0.50f))) {
							buf_y = 0;
							d1.run = 0;
						}
					} else if (buf_x && p1.body.grounded) {
						if (g_meter >= p_meter_cost() &&
						    exo_fighter_attack(&p1, &g_special)) {
							buf_x = 0;
							d1.run = 0;
							g_meter = (uint8_t)(g_meter - p_meter_cost());
						}
					}
				} else {
					p1.body.vx = 0.0f;
					d1.run = 0;
				}

				if (p2.phase != EXO_PHASE_GRABBED) {
					if (exo_fighter_can_act(&p2) || p2.phase == EXO_PHASE_BLOCK) {
						exo_fighter_guard(&p2, want_block(&p2, a2, sh));
						if (exo_fighter_can_act(&p2)) {
							control(&p2.body, a2, false, &d2);
							if (exo_down(EXO_BTN_UP))
								exo_fighter_attack(&p2, &g_punch);
						} else {
							p2.body.vx = 0.0f;
						}
					} else {
						p2.body.vx = 0.0f;
					}
				}

				exo_body_integrate(&p1.body, dt, GRAVITY, GROUND_Y);
				exo_body_integrate(&p2.body, dt, GRAVITY, GROUND_Y);

				if (p1.phase == EXO_PHASE_GRAB)
					snap_grab(&p1, &p2);
				else
					exo_body_separate(&p1.body, &p2.body, 0.0f, STAGE_W);

				if (exo_fighter_can_act(&p1) || p1.phase == EXO_PHASE_BLOCK)
					exo_body_face(&p1.body, &p2.body);
				if (exo_fighter_can_act(&p2) || p2.phase == EXO_PHASE_BLOCK)
					exo_body_face(&p2.body, &p1.body);

				exo_fighter_tick(&p1);
				exo_fighter_tick(&p2);
				if (p1.phase != EXO_PHASE_GRAB) {
					resolve_hits(&p1, &p2, &hitstop);
					resolve_hits(&p2, &p1, &hitstop);
				}
				if (p2.phase != EXO_PHASE_HITSTUN &&
				    p2.phase != EXO_PHASE_GRABBED &&
				    p1.phase == EXO_PHASE_IDLE)
					g_combo = 0;
			} else {
				ko_timer++;
				if (ko_timer == KO_WAIT) {
					match_award(&p1, &p2);
					if (!g_match.over) {
						reset_round(&p1, &p2, &d1);
						ko_timer = 0;
						hitstop = 0;
					}
				} else if (g_match.over &&
				           ko_timer >= KO_WAIT + MATCH_WAIT) {
					match_init();
					reset_round(&p1, &p2, &d1);
					ko_timer = 0;
					hitstop = 0;
				}
			}
		}

		mid = (p1.body.x + p1.body.w * 0.5f +
		       p2.body.x + p2.body.w * 0.5f) * 0.5f;
		cam = clampf(mid - EXO_TOP_W * 0.5f, 0.0f, STAGE_W - EXO_TOP_W);

		exo_render_begin();
		exo_render_eye(EXO_EYE_LEFT, clear);
		draw_world(EXO_EYE_LEFT, cam, &p1, &p2);
		draw_guard(&p1, cam, EXO_EYE_LEFT);
		draw_guard(&p2, cam, EXO_EYE_LEFT);
		draw_hitboxes(&p1, &p2, cam, EXO_EYE_LEFT);
		draw_hp_top(&p1, &p2, EXO_EYE_LEFT);
		draw_combo_top(EXO_EYE_LEFT);
		exo_render_eye(EXO_EYE_RIGHT, clear);
		draw_world(EXO_EYE_RIGHT, cam, &p1, &p2);
		draw_guard(&p1, cam, EXO_EYE_RIGHT);
		draw_guard(&p2, cam, EXO_EYE_RIGHT);
		draw_hitboxes(&p1, &p2, cam, EXO_EYE_RIGHT);
		draw_hp_top(&p1, &p2, EXO_EYE_RIGHT);
		draw_combo_top(EXO_EYE_RIGHT);
		exo_render_bottom(bot);
		draw_bottom(&p1, &p2, &d1, paused);
		exo_render_end();
		exo_frame_end();
	}

	if (g_sheet)
		C2D_SpriteSheetFree(g_sheet);
	exo_shutdown();
	return 0;
}

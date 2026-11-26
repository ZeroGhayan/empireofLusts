#include "exo/aero.h"
#include <math.h>

#define AERO_G 90.0f /* mesma gravidade do EXO_FLIGHT_GRAV */

/*
 * Numeros iniciais para afinar no hardware (docs/voo.md, secao "Afinacao").
 * Shirammy: topo alto, planeio longo (arrasto baixo), mergulho forte,
 *           curvas lentas, batida modesta.
 * Rexxi:    topo menor, arrasto maior, batida explosiva com recarga curta,
 *           rolagem rapida e curvas fechadas.
 */
static const ExoAeroProfile PROF[2] = {
	{
		.name = "Shirammy",
		.v_min = 110.0f, .v_cruise = 260.0f, .v_max = 380.0f,
		.drag_k = 3.6e-4f,
		.gamma_up = 0.45f, .gamma_down = 0.75f,
		.pitch_rate = 0.90f, .roll_rate = 1.60f, .bank_max = 1.00f,
		.turn_g = 150.0f, .turn_rate_max = 0.95f, .turn_bleed = 0.06f,
		.rudder_rate = 0.25f,
		.flap_dv = 26.0f, .flap_gamma = 0.05f, .flap_vy = 6.0f, .flap_cd = 0.70f,
		.stall_margin = 40.0f, .stall_nose = 0.28f,
		.brake_drag = 2.5f,
		.pierce_speed = 330.0f, .barrier_window = 0.0f
	},
	{
		.name = "Rexxi",
		.v_min = 70.0f, .v_cruise = 150.0f, .v_max = 200.0f,
		.drag_k = 1.6e-3f,
		.gamma_up = 0.60f, .gamma_down = 0.70f,
		.pitch_rate = 1.80f, .roll_rate = 3.20f, .bank_max = 1.25f,
		.turn_g = 220.0f, .turn_rate_max = 2.20f, .turn_bleed = 0.04f,
		.rudder_rate = 0.45f,
		.flap_dv = 55.0f, .flap_gamma = 0.12f, .flap_vy = 18.0f, .flap_cd = 0.28f,
		.stall_margin = 25.0f, .stall_nose = 0.25f,
		.brake_drag = 2.0f,
		.pierce_speed = 0.0f, .barrier_window = 0.35f
	}
};

static float clampf(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

static float approach(float v, float target, float rate, float dt)
{
	float d = target - v;
	float m = rate * dt;
	if (d > m) return v + m;
	if (d < -m) return v - m;
	return target;
}

const ExoAeroProfile *exo_aero_profile(int pilot)
{
	return &PROF[(pilot == 1) ? 1 : 0];
}

void exo_aero_enter(ExoAeroState *s, const ExoAeroProfile *p,
                    float speed, float gamma, float yaw)
{
	s->speed = clampf(speed, p->v_min, p->v_max);
	s->gamma = clampf(gamma, -p->gamma_down, p->gamma_up);
	s->roll = 0.0f;
	s->yaw = yaw;
	s->flap_t = 0.0f;
	s->since_flap = 99.0f;
	s->stalling = 0;
}

void exo_aero_step(ExoAeroState *s, const ExoAeroProfile *p,
                   const ExoAeroInput *in, float dt, ExoAeroOut *out)
{
	float lift_frac, up_lim, g_target, roll_target, yaw_rate, drag, dv, hv;
	float pitch_in = in ? clampf(in->pitch, -1.0f, 1.0f) : 0.0f;
	float roll_in = in ? clampf(in->roll, -1.0f, 1.0f) : 0.0f;
	float rud_in = in ? clampf(in->rudder, -1.0f, 1.0f) : 0.0f;
	int brake = in && in->brake;

	if (dt <= 0.0f)
		dt = 1.0f / 60.0f;
	out->flapped = 0;

	/* 1. bater asas (A): impulso com recarga; segurar A repete */
	if (s->flap_t > 0.0f)
		s->flap_t -= dt;
	s->since_flap += dt;
	if (in && in->flap && s->flap_t <= 0.0f) {
		s->speed += p->flap_dv;
		s->gamma += p->flap_gamma;
		s->flap_t = p->flap_cd;
		s->since_flap = 0.0f;
		s->flaps++;
		out->flapped = 1;
	}

	/* 2. sustentacao disponivel: 0 no estol, 1 a partir do cruzeiro */
	lift_frac = (s->speed - p->v_min) / (p->v_cruise - p->v_min);
	lift_frac = clampf(lift_frac, 0.0f, 1.0f);
	up_lim = p->gamma_up * lift_frac;

	/* 3. angulo de trajetoria pedido (stick para cima = nariz para baixo) */
	g_target = (pitch_in < 0.0f) ? (-pitch_in * up_lim)
	                             : (-pitch_in * p->gamma_down);

	/* 4. assistencia de estol: nunca parar no ar, o nariz desce sozinho */
	s->stalling = 0;
	if (s->speed < p->v_min + p->stall_margin) {
		float t = (p->v_min + p->stall_margin - s->speed) / p->stall_margin;
		float nose = -p->stall_nose * clampf(t, 0.0f, 1.0f);
		if (g_target > nose)
			g_target = nose;
		s->stalling = (t > 0.5f);
	}
	s->gamma = approach(s->gamma, g_target, p->pitch_rate, dt);
	s->gamma = clampf(s->gamma, -p->gamma_down, p->gamma_up);

	/* 5. rolagem e curva coordenada */
	roll_target = roll_in * p->bank_max;
	s->roll = approach(s->roll, roll_target, p->roll_rate, dt);
	yaw_rate = (p->turn_g * (brake ? 1.25f : 1.0f)) * tanf(s->roll) /
	           (s->speed > 1.0f ? s->speed : 1.0f);
	yaw_rate = clampf(yaw_rate, -p->turn_rate_max, p->turn_rate_max);
	yaw_rate += rud_in * p->rudder_rate;
	/* convencao do ExoFlight: frente = (sin yaw, cos yaw), +X = direita da
	   tela, entao stick X positivo (direita) aumenta o yaw */
	s->yaw += yaw_rate * dt;
	while (s->yaw > 3.14159265f) s->yaw -= 6.2831853f;
	while (s->yaw < -3.14159265f) s->yaw += 6.2831853f;

	/* 6. energia: gravidade ao longo da trajetoria, arrasto, curva */
	drag = p->drag_k * s->speed * s->speed * (brake ? p->brake_drag : 1.0f);
	dv = -AERO_G * sinf(s->gamma) - drag
	     - p->turn_bleed * fabsf(tanf(s->roll)) * s->speed * 0.1f;
	s->speed += dv * dt;
	/* piso: perto do estol ainda anda para frente (nunca paira) */
	s->speed = clampf(s->speed, p->v_min * 0.85f, p->v_max);

	/* 7. velocidade no mundo */
	hv = s->speed * cosf(s->gamma);
	out->vx = sinf(s->yaw) * hv;
	out->vz = cosf(s->yaw) * hv;
	out->vy = s->speed * sinf(s->gamma) +
	          (s->since_flap < 0.2f ? p->flap_vy * (1.0f - s->since_flap / 0.2f) : 0.0f);
}

int exo_aero_can_pierce_ice(const ExoAeroState *s, const ExoAeroProfile *p)
{
	return p->pierce_speed > 0.0f && s->speed >= p->pierce_speed;
}

int exo_aero_can_pass_barrier(const ExoAeroState *s, const ExoAeroProfile *p)
{
	return p->barrier_window > 0.0f && s->since_flap <= p->barrier_window;
}

float exo_aero_turn_radius(const ExoAeroProfile *p, float speed)
{
	float w = p->turn_g * tanf(p->bank_max) / (speed > 1.0f ? speed : 1.0f);
	if (w > p->turn_rate_max) w = p->turn_rate_max;
	return speed / (w > 1e-4f ? w : 1e-4f);
}

float exo_aero_uturn_time(const ExoAeroProfile *p, float speed)
{
	float w = p->turn_g * tanf(p->bank_max) / (speed > 1.0f ? speed : 1.0f);
	if (w > p->turn_rate_max) w = p->turn_rate_max;
	return 3.14159265f / (w > 1e-4f ? w : 1e-4f) + p->bank_max / p->roll_rate;
}

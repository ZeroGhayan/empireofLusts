#ifndef EXO_AERO_H
#define EXO_AERO_H

/*
 * Fisica de voo "aviao" das wyverns (modo de voo livre, ver docs/voo.md).
 *
 * Planar e o estado padrao: sem bater asas a velocidade cai pelo arrasto,
 * mergulhar troca altura por velocidade, subir troca velocidade por altura.
 * A (segurado ou tocado) = bater de asas: impulso de velocidade + leve
 * subida, com tempo de recarga proprio de cada piloto. Nunca ha pairar:
 * perto da velocidade de estol o nariz desce sozinho (assistencia) e a
 * velocidade tem piso. Curvas sao coordenadas: inclinar (roll) gera taxa de
 * guinada g*tan(roll)/v, entao quem voa rapido curva mais aberto.
 *
 * Modulo puro (so math.h), sem 3DS: da para testar no PC.
 */

typedef struct ExoAeroProfile {
	const char *name;
	float v_min;          /* velocidade de estol (u/s); abaixo disso so mergulho */
	float v_cruise;       /* velocidade "confortavel" de cruzeiro */
	float v_max;          /* teto absoluto (igual a vmax do HUD) */
	float drag_k;         /* desaceleracao = drag_k * v^2 */
	float gamma_up;       /* maximo angulo de subida (rad) com velocidade plena */
	float gamma_down;     /* maximo angulo de mergulho (rad) */
	float pitch_rate;     /* rad/s de mudanca do angulo de trajetoria */
	float roll_rate;      /* rad/s de rolagem */
	float bank_max;       /* rolagem maxima (rad) */
	float turn_g;         /* "g" da curva coordenada: yaw' = turn_g*tan(roll)/v */
	float turn_rate_max;  /* limite de guinada (rad/s) */
	float turn_bleed;     /* perda de velocidade por curva fechada */
	float rudder_rate;    /* L/R: guinada direta pequena (rad/s) */
	float flap_dv;        /* impulso de velocidade por batida (u/s) */
	float flap_gamma;     /* empurrao de nariz para cima por batida (rad) */
	float flap_vy;        /* empurrao vertical direto por batida (u/s) */
	float flap_cd;        /* recarga entre batidas (s) */
	float stall_margin;   /* faixa acima de v_min onde a assistencia atua */
	float stall_nose;     /* angulo de mergulho imposto no estol (rad) */
	float brake_drag;     /* multiplicador de arrasto com B (freio aerodinamico) */
	float pierce_speed;   /* >0: velocidade para furar a camada de gelo (Shirammy) */
	float barrier_window; /* >0: s apos uma batida em que passa a barreira (Rexxi) */
} ExoAeroProfile;

typedef struct ExoAeroInput {
	float pitch;   /* -1..1, stick Y (para cima = nariz para baixo, como aviao) */
	float roll;    /* -1..1, stick X */
	float rudder;  /* -1..1, R - L */
	int   flap;    /* A segurado: bate sempre que a recarga deixa */
	int   brake;   /* B segurado */
} ExoAeroInput;

typedef struct ExoAeroState {
	float speed;    /* velocidade no ar, ao longo da trajetoria */
	float gamma;    /* angulo da trajetoria (rad, + sobe) */
	float roll;     /* rolagem atual (rad) */
	float yaw;      /* rumo (rad), mesma convencao do ExoFlight */
	float flap_t;   /* recarga restante */
	float since_flap;
	int   stalling; /* 1 enquanto a assistencia de estol esta ativa */
	int   flaps;    /* contador de batidas (HUD/perfil) */
} ExoAeroState;

/* saidas por passo, para o integrador do ExoFlight aplicar */
typedef struct ExoAeroOut {
	float vx, vy, vz; /* velocidade no mundo (u/s) */
	int   flapped;    /* 1 no quadro em que bateu asas */
} ExoAeroOut;

const ExoAeroProfile *exo_aero_profile(int pilot); /* 0 Shirammy, 1 Rexxi */

void exo_aero_enter(ExoAeroState *s, const ExoAeroProfile *p,
                    float speed, float gamma, float yaw);
void exo_aero_step(ExoAeroState *s, const ExoAeroProfile *p,
                   const ExoAeroInput *in, float dt, ExoAeroOut *out);

/* ganchos de jogo (mapas ainda nao existem): */
int  exo_aero_can_pierce_ice(const ExoAeroState *s, const ExoAeroProfile *p);
int  exo_aero_can_pass_barrier(const ExoAeroState *s, const ExoAeroProfile *p);

/* raio de curva aproximado (u) e tempo de meia-volta (s) a uma velocidade */
float exo_aero_turn_radius(const ExoAeroProfile *p, float speed);
float exo_aero_uturn_time(const ExoAeroProfile *p, float speed);

#endif

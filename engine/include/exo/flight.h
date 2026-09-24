#ifndef EXO_FLIGHT_H
#define EXO_FLIGHT_H

#include "exo/types.h"
#include "exo/input.h"
#include "exo/tilemap.h"

#define EXO_FLIGHT_CELL        32.0f
#define EXO_FLIGHT_SLOPE_TAN   2.747f
#define EXO_FLIGHT_RENDER      16
#define EXO_FLIGHT_HYST_ENTER  20.0f
#define EXO_FLIGHT_HYST_LEAVE  10.0f
#define EXO_FLIGHT_NEAR        6.0f
#define EXO_FLIGHT_GRAV        90.0f
#define EXO_FLIGHT_FOCAL       210.0f

typedef enum ExoFlightMode {
	EXO_FLIGHT_LOW = 0,
	EXO_FLIGHT_HIGH
} ExoFlightMode;

typedef enum ExoPilot {
	EXO_PILOT_SHIRAMMY = 0,
	EXO_PILOT_REXXI
} ExoPilot;

typedef struct ExoPilotStats {
	float vmax;
	float walk;
	float accel;
	float coast;
	float turn_low;
	float turn_high;
	float jump;
	float hud_scale;
	const char *hud_unit;
	const char *name;
} ExoPilotStats;

typedef struct ExoFlight {
	ExoTilemap map;
	ExoPilot   pilot;
	ExoFlightMode mode;
	float x, y, z;
	float yaw;
	float speed;
	float vy;
	float cam_x, cam_z, cam_h, cam_dist;
	float horizon;
	int   grounded;
	int   cell_x, cell_z;
	int   tiles_drawn;
} ExoFlight;

const ExoPilotStats *exo_pilot_stats(ExoPilot p);

void  exo_flight_init(ExoFlight *f, ExoPilot pilot);
void  exo_flight_set_pilot(ExoFlight *f, ExoPilot pilot);
void  exo_flight_tick(ExoFlight *f, const ExoInput *in, float dt);

float exo_flight_hud_speed(const ExoFlight *f);
void  exo_flight_eye_offset(const ExoFlight *f, float slider, int eye_sign,
                            float *ox, float *oy);

int   exo_flight_project(const ExoFlight *f, float wx, float wz,
                         float eye_x, float *sx, float *sy);

/* Recorta o quad do tile no plano near e devolve 3..6 vértices de ecrã. */
int   exo_flight_clip_quad(const ExoFlight *f, float wx, float wz, float cell,
                           float eye_x, float sx[6], float sy[6], int *nv);

#endif

#ifndef EXO_FLIGHT_H
#define EXO_FLIGHT_H

#include "exo/types.h"
#include "exo/input.h"
#include "exo/tilemap.h"

#define EXO_FLIGHT_CELL          32.0f
#define EXO_FLIGHT_SLOPE_TAN     2.747f
#define EXO_FLIGHT_RENDER        18
#define EXO_FLIGHT_RENDER_MIN    4
#define EXO_FLIGHT_RENDER_MAX    20
#define EXO_FLIGHT_HYST_ENTER    20.0f
#define EXO_FLIGHT_HYST_LEAVE    10.0f
#define EXO_FLIGHT_NEAR          6.0f
#define EXO_FLIGHT_GRAV          90.0f
#define EXO_FLIGHT_FOCAL         210.0f
#define EXO_FLIGHT_REXXI_VMAX    200.0f
#define EXO_FLIGHT_Y_MAX         180.0f
#define EXO_FLIGHT_HUD_SHI       1062.0f
#define EXO_FLIGHT_HUD_REX       2.5f
#define EXO_FLIGHT_CHARGE_SEC    0.80f
#define EXO_FLIGHT_BANK_MAX      0.42f

typedef enum ExoFlightMode {
	EXO_FLIGHT_LOW = 0,
	EXO_FLIGHT_HIGH
} ExoFlightMode;

typedef enum ExoPilot {
	EXO_PILOT_SHIRAMMY = 0,
	EXO_PILOT_REXXI
} ExoPilot;

typedef enum ExoCamRef {
	EXO_CAM_SURFACE = 0,
	EXO_CAM_PILOT
} ExoCamRef;

typedef struct ExoPilotStats {
	float vmax;
	float walk;
	float accel;
	float coast;
	float turn_low;
	float turn_high;
	float jump;
	float climb;
	float hud_max;
	const char *hud_unit;
	const char *name;
} ExoPilotStats;

typedef struct ExoFlight {
	ExoTilemap map;
	ExoPilot   pilot;
	ExoFlightMode mode;
	ExoCamRef  cam_ref;
	float x, y, z;
	float yaw;
	float speed;
	float cruise;
	float vy;
	float cam_x, cam_z, cam_h, cam_dist;
	float horizon;
	float focal;
	float bank;
	float charge;
	int   grounded;
	int   flying;
	int   cell_x, cell_z;
	int   tiles_drawn;
	int   tiles_culled;
	int   render_r;
	int   draw_cap;
	float pad_x, pad_y;
	float move_x, move_z;
	float wish_x, wish_z;
} ExoFlight;

const ExoPilotStats *exo_pilot_stats(ExoPilot p);

void  exo_flight_init(ExoFlight *f, ExoPilot pilot);
void  exo_flight_set_pilot(ExoFlight *f, ExoPilot pilot);
void  exo_flight_tick(ExoFlight *f, const ExoInput *in, float dt);
float exo_flight_vmax(const ExoFlight *f);
float exo_flight_hud_speed(const ExoFlight *f);
float exo_flight_hud_max(const ExoFlight *f);
float exo_flight_speed_frac(const ExoFlight *f);
void  exo_flight_eye_offset(const ExoFlight *f, float slider, int eye_sign,
                            float *ox, float *oy);

int   exo_flight_project(const ExoFlight *f, float wx, float wz,
                         float eye_x, float *sx, float *sy);

int   exo_flight_clip_quad(const ExoFlight *f, float wx, float wz, float cell,
                           float eye_x, float sx[6], float sy[6], int *nv);

int   exo_flight_tile_visible(const ExoFlight *f, float wx, float wz,
                              float cell, float eye_x, float far_z, float *lz);

void  exo_flight_apply_ref(const ExoFlight *f, float *sx, float *sy);

#endif

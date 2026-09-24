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
#define EXO_FLIGHT_HYST_ENTER    72.0f
#define EXO_FLIGHT_HYST_LEAVE    28.0f
#define EXO_FLIGHT_NEAR          6.0f
#define EXO_FLIGHT_GRAV          90.0f
#define EXO_FLIGHT_FOCAL         210.0f
#define EXO_FLIGHT_REXXI_VMAX    200.0f
#define EXO_FLIGHT_Y_MAX         1000.0f
#define EXO_FLIGHT_HUD_SHI       1062.0f
#define EXO_FLIGHT_HUD_REX       2.5f
#define EXO_FLIGHT_CHARGE_SEC    1.60f
#define EXO_FLIGHT_CHARGE_DECAY  9.00f
#define EXO_FLIGHT_BANK_MAX      0.42f
#define EXO_FLIGHT_PITCH_MAX     0.55f
#define EXO_FLIGHT_SPRING_VY     110.0f
#define EXO_FLIGHT_PIN_LOW       198.0f
#define EXO_FLIGHT_PIN_FLY       160.0f
#define EXO_FLIGHT_TREE_MAX      16
#define EXO_FLIGHT_FAKE_MAX      3

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

typedef enum ExoRunState {
	EXO_RUN_WAIT = 0,
	EXO_RUN_GO,
	EXO_RUN_DONE
} ExoRunState;

typedef enum ExoCourse {
	EXO_COURSE_DEMO = 0,
	EXO_COURSE_SS4,
	EXO_COURSE_DP1
} ExoCourse;

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

typedef struct ExoTree {
	int   cx, cz;
	float h;
} ExoTree;

typedef struct ExoFlight {
	ExoTilemap map;
	ExoPilot   pilot;
	ExoFlightMode mode;
	ExoCamRef  cam_ref;
	ExoCourse  course;
	float x, y, z;
	float yaw;
	float pitch;
	float speed;
	float cruise;
	float vy;
	float cam_x, cam_z, cam_h, cam_dist;
	float horizon;
	float focal;
	float bank;
	float charge;
	int   charge_armed;
	int   grounded;
	int   flying;
	int   cell_x, cell_z;
	int   tiles_drawn;
	int   tiles_culled;
	int   render_r;
	int   draw_cap;
	float spring_cd;
	float pad_x, pad_y;
	float move_x, move_z;
	float wish_x, wish_z;
	ExoTree trees[EXO_FLIGHT_TREE_MAX];
	int    tree_n;
	ExoRunState run;
	float run_t;
	float best_t;
	int    laps;
	int    lap_chk;
	int    goal_cx, goal_cz;
	int    fake_n;
	int    fake_cx[EXO_FLIGHT_FAKE_MAX];
	int    fake_cz[EXO_FLIGHT_FAKE_MAX];
} ExoFlight;

const ExoPilotStats *exo_pilot_stats(ExoPilot p);

void  exo_flight_init(ExoFlight *f, ExoPilot pilot);
void  exo_flight_set_pilot(ExoFlight *f, ExoPilot pilot);
void  exo_flight_reset_run(ExoFlight *f);
int   exo_flight_load_map(ExoFlight *f, ExoCourse course, const void *etm, uint32_t size);
void  exo_flight_tick(ExoFlight *f, const ExoInput *in, float dt);
float exo_flight_vmax(const ExoFlight *f);
float exo_flight_hud_speed(const ExoFlight *f);
float exo_flight_hud_max(const ExoFlight *f);
float exo_flight_speed_frac(const ExoFlight *f);
void  exo_flight_eye_offset(const ExoFlight *f, float slider, int eye_sign,
                            float *ox, float *oy);

int   exo_flight_project(const ExoFlight *f, float wx, float wz,
                         float eye_x, float *sx, float *sy);
int   exo_flight_project3(const ExoFlight *f, float wx, float wy, float wz,
                          float eye_x, float *sx, float *sy);

int   exo_flight_clip_quad(const ExoFlight *f, float wx, float wz, float cell,
                           float eye_x, float sx[6], float sy[6], int *nv);

int   exo_flight_tile_visible(const ExoFlight *f, float wx, float wz,
                              float cell, float eye_x, float far_z, float *lz);

void  exo_flight_apply_ref(const ExoFlight *f, float *sx, float *sy);

#endif

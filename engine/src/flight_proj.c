#include "exo/flight.h"
#include <math.h>

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
	if (f->speed < 0.0f)
		return 0.0f;
	if (f->speed > vmax)
		return 1.0f;
	return f->speed / vmax;
}

void exo_flight_eye_offset(const ExoFlight *f, float slider, int eye_sign,
                           float *ox, float *oy)
{
	(void)f;
	*ox = slider * 2.4f * (float)eye_sign;
	*oy = 0.0f;
}

void exo_flight_cam_update(ExoFlight *f)
{
	float b;
	f->cam_cy = cosf(f->cam_yaw);
	f->cam_sy = sinf(f->cam_yaw);
	f->cam_cp = cosf(-f->pitch);
	f->cam_sp = sinf(-f->pitch);
	f->ref_on = !(fabsf(f->bank) < 0.002f && fabsf(f->slope_roll) < 0.002f);
	b = f->bank * 0.35f + f->slope_roll * EXO_FLIGHT_CAM_ROLL;
	f->ref_c = cosf(b);
	f->ref_s = sinf(b);
}

/* antes: 2 cosf + 2 sinf por chamada (4 por canto de tile, 2 olhos);
   agora le o cache montado por exo_flight_cam_update(). */
static void to_cam(const ExoFlight *f, float wx, float wz, float eye,
                   float *lx, float *lz)
{
	float dx = wx - f->cam_x;
	float dz = wz - f->cam_z;
	*lx =  dx * f->cam_cy - dz * f->cam_sy + eye;
	*lz =  dx * f->cam_sy + dz * f->cam_cy;
}

/* retorna 0 se, depois de aplicar o pitch, o ponto ficou atras do plano
   proximo: antes ele era so "grudado" em NEAR e virava uma ponta esticada
   na tela (um dos glitches de tile gigante ao inclinar a camera). */
static int cam_to_screen(const ExoFlight *f, float lx, float lz, float wy,
                         float *sx, float *sy)
{
	int ok;
	float ly = f->cam_h - wy;
	float k;
	float cp = f->cam_cp;
	float sp = f->cam_sp;
	float ly2 = ly * cp - lz * sp;
	float lz2 = ly * sp + lz * cp;

	ly = ly2;
	lz = lz2;
	ok = lz >= EXO_FLIGHT_NEAR * 0.5f;
	if (lz < EXO_FLIGHT_NEAR)
		lz = EXO_FLIGHT_NEAR;
	k = EXO_FLIGHT_FOCAL / lz;
	*sx = 200.0f + lx * k;
	*sy = f->horizon + ly * k;
	return ok;
}

void exo_flight_apply_ref(const ExoFlight *f, float *sx, float *sy)
{
	float cx = 200.0f;
	float cy = f->horizon;
	float c, s, dx, dy;

	if (!f->ref_on)
		return;
	c = f->ref_c;
	s = f->ref_s;
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
	if (!cam_to_screen(f, lx, lz, wy, sx, sy))
		return 0;
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
		(void)cam_to_screen(f, olx[i], olz[i], 0.0f, &sx[i], &sy[i]);
		exo_flight_apply_ref(f, &sx[i], &sy[i]);
	}
	*nv = n;
	return 1;
}

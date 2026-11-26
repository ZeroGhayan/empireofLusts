#include "exo/prof.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#ifdef __3DS__
#include <3ds.h>
static uint64_t now_ticks(void) { return svcGetSystemTick(); }
static float ticks_to_ms(uint64_t t) { return (float)((double)t / CPU_TICKS_PER_MSEC); }
#define PROF_ROOT "sdmc:/3ds/"
#else
#include <time.h>
static uint64_t now_ticks(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static float ticks_to_ms(uint64_t t) { return (float)((double)t / 1.0e6); }
#define PROF_ROOT "./"
#endif

#define EMA_K       0.06f
#define PEAK_WINDOW 60u
#define ROW_BYTES   160

static ExoProfStats g_st;
static const char *g_phase_name[EXO_PROF_MAX_PHASES];
static const char *g_counter_name[EXO_PROF_MAX_COUNTERS];
static int g_np, g_nc;
static char g_dir[64];
static char g_path[96];
static char g_tag[16];

static uint64_t g_frame_t0, g_session_t0;
static uint64_t g_phase_t0[EXO_PROF_MAX_PHASES];
static uint64_t g_phase_acc[EXO_PROF_MAX_PHASES];
static int32_t  g_counter[EXO_PROF_MAX_COUNTERS];
static float    g_peak_run[EXO_PROF_MAX_PHASES + 1];
static float    g_last_flush_ms;

static FILE *g_fp;
static char  g_rows[EXO_PROF_ROWS * ROW_BYTES];
static size_t g_rows_len;
static int   g_rows_n;

void exo_prof_init(const char *app_dir,
                   const char *const *phase_names, int n_phases,
                   const char *const *counter_names, int n_counters)
{
	int i;
	memset(&g_st, 0, sizeof(g_st));
	g_st.log_index = -1;
	g_np = n_phases > EXO_PROF_MAX_PHASES ? EXO_PROF_MAX_PHASES : n_phases;
	g_nc = n_counters > EXO_PROF_MAX_COUNTERS ? EXO_PROF_MAX_COUNTERS : n_counters;
	if (g_np < 0) g_np = 0;
	if (g_nc < 0) g_nc = 0;
	for (i = 0; i < g_np; ++i)
		g_phase_name[i] = phase_names[i];
	for (i = 0; i < g_nc; ++i)
		g_counter_name[i] = counter_names[i];
	snprintf(g_dir, sizeof(g_dir), "%s%s", PROF_ROOT, app_dir ? app_dir : "exo");
	g_tag[0] = 0;
	g_fp = NULL;
	g_rows_len = 0;
	g_rows_n = 0;
	g_session_t0 = now_ticks();
	g_frame_t0 = g_session_t0;
}

static void flush_rows(void)
{
	uint64_t t0;
	if (!g_fp || g_rows_len == 0)
		return;
	t0 = now_ticks();
	fwrite(g_rows, 1, g_rows_len, g_fp);
	fflush(g_fp);
	g_rows_len = 0;
	g_rows_n = 0;
	g_last_flush_ms = ticks_to_ms(now_ticks() - t0);
}

void exo_prof_shutdown(void)
{
	exo_prof_log_stop();
}

void exo_prof_frame_begin(void)
{
	int i;
	g_frame_t0 = now_ticks();
	for (i = 0; i < g_np; ++i)
		g_phase_acc[i] = 0;
	for (i = 0; i < g_nc; ++i)
		g_counter[i] = 0;
}

void exo_prof_begin(int phase)
{
	if (phase < 0 || phase >= g_np)
		return;
	g_phase_t0[phase] = now_ticks();
}

void exo_prof_end(int phase)
{
	if (phase < 0 || phase >= g_np)
		return;
	g_phase_acc[phase] += now_ticks() - g_phase_t0[phase];
}

void exo_prof_counter(int idx, int32_t value)
{
	if (idx >= 0 && idx < g_nc)
		g_counter[idx] = value;
}

void exo_prof_gpu(float proc_ms, float draw_ms)
{
	g_st.gpu_proc_ms = proc_ms;
	g_st.gpu_draw_ms = draw_ms;
}

void exo_prof_tag(const char *tag)
{
	size_t i;
	if (!tag) {
		g_tag[0] = 0;
		return;
	}
	for (i = 0; i + 1 < sizeof(g_tag) && tag[i]; ++i)
		g_tag[i] = (tag[i] == ',' || tag[i] == '\n') ? '_' : tag[i];
	g_tag[i] = 0;
}

static void append_row(float flushed_ms)
{
	char *p;
	int n, i, room;

	if (g_rows_len + ROW_BYTES > sizeof(g_rows))
		flush_rows();
	p = g_rows + g_rows_len;
	room = (int)(sizeof(g_rows) - g_rows_len);
	n = snprintf(p, (size_t)room, "%lu,%.2f,%.3f",
	             (unsigned long)g_st.frame,
	             (double)ticks_to_ms(g_frame_t0 - g_session_t0),
	             (double)g_st.frame_ms);
	for (i = 0; i < g_np && n < room; ++i)
		n += snprintf(p + n, (size_t)(room - n), ",%.3f",
		              (double)g_st.last_ms[i]);
	if (n < room)
		n += snprintf(p + n, (size_t)(room - n), ",%.3f,%.3f",
		              (double)g_st.gpu_proc_ms, (double)g_st.gpu_draw_ms);
	for (i = 0; i < g_nc && n < room; ++i)
		n += snprintf(p + n, (size_t)(room - n), ",%ld", (long)g_counter[i]);
	if (n < room)
		n += snprintf(p + n, (size_t)(room - n), ",%.2f,%s\n",
		              (double)flushed_ms, g_tag);
	if (n >= room)
		n = room - 1;
	g_rows_len += (size_t)n;
	g_rows_n++;
}

void exo_prof_frame_end(void)
{
	int i;
	uint64_t t1 = now_ticks();
	float flushed = g_last_flush_ms;

	g_last_flush_ms = 0.0f;
	g_st.frame_ms = ticks_to_ms(t1 - g_frame_t0);
	for (i = 0; i < g_np; ++i) {
		float ms = ticks_to_ms(g_phase_acc[i]);
		g_st.last_ms[i] = ms;
		g_st.avg_ms[i] += (ms - g_st.avg_ms[i]) * EMA_K;
		if (ms > g_peak_run[i])
			g_peak_run[i] = ms;
	}
	g_st.frame_avg_ms += (g_st.frame_ms - g_st.frame_avg_ms) * EMA_K;
	if (g_st.frame_ms > g_peak_run[EXO_PROF_MAX_PHASES])
		g_peak_run[EXO_PROF_MAX_PHASES] = g_st.frame_ms;
	if ((g_st.frame % PEAK_WINDOW) == PEAK_WINDOW - 1) {
		for (i = 0; i < g_np; ++i) {
			g_st.peak_ms[i] = g_peak_run[i];
			g_peak_run[i] = 0.0f;
		}
		g_st.frame_peak_ms = g_peak_run[EXO_PROF_MAX_PHASES];
		g_peak_run[EXO_PROF_MAX_PHASES] = 0.0f;
	}
	if (g_fp) {
		append_row(flushed);
		g_st.rows_written++;
		if (g_rows_n >= EXO_PROF_ROWS)
			flush_rows();
	}
	g_st.frame++;
}

int exo_prof_log_start(void)
{
	int i, idx;
	FILE *probe;

	if (g_fp)
		return 1;
#ifdef __3DS__
	mkdir(PROF_ROOT, 0777);
#endif
	mkdir(g_dir, 0777);
	for (idx = 0; idx < 1000; ++idx) {
		snprintf(g_path, sizeof(g_path), "%s/prof_%03d.csv", g_dir, idx);
		probe = fopen(g_path, "rb");
		if (!probe)
			break;
		fclose(probe);
	}
	if (idx >= 1000)
		return 0;
	g_fp = fopen(g_path, "wb");
	if (!g_fp)
		return 0;
	fprintf(g_fp, "frame,t_ms,frame_ms");
	for (i = 0; i < g_np; ++i)
		fprintf(g_fp, ",%s_ms", g_phase_name[i]);
	fprintf(g_fp, ",gpu_proc_ms,gpu_draw_ms");
	for (i = 0; i < g_nc; ++i)
		fprintf(g_fp, ",%s", g_counter_name[i]);
	fprintf(g_fp, ",flush_ms,tag\n");
	fflush(g_fp);
	g_rows_len = 0;
	g_rows_n = 0;
	g_st.logging = 1;
	g_st.log_index = idx;
	g_st.rows_written = 0;
	return 1;
}

void exo_prof_log_stop(void)
{
	if (!g_fp)
		return;
	flush_rows();
	fclose(g_fp);
	g_fp = NULL;
	g_st.logging = 0;
}

int exo_prof_log_toggle(void)
{
	if (g_fp) {
		exo_prof_log_stop();
		return 0;
	}
	return exo_prof_log_start();
}

const ExoProfStats *exo_prof_stats(void) { return &g_st; }

const char *exo_prof_phase_name(int phase)
{
	if (phase < 0 || phase >= g_np)
		return "";
	return g_phase_name[phase];
}

int exo_prof_phase_count(void) { return g_np; }

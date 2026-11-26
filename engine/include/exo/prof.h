#ifndef EXO_PROF_H
#define EXO_PROF_H

/*
 * Perfilador de quadro (frame profiler) do motor EXO.
 *
 * Modulo autocontido, sem dependencia do resto do motor: pode ser copiado
 * tal como esta para o Battle in Megaville (megaville-3ds/engine).
 *
 * Uso por quadro:
 *   exo_prof_frame_begin();
 *   exo_prof_begin(FASE); ... exo_prof_end(FASE);   (quantas fases quiser)
 *   exo_prof_counter(i, valor);                      (tiles, vertices...)
 *   exo_prof_frame_end();
 *
 * Os tempos sao de CPU (svcGetSystemTick). O tempo de GPU do quadro
 * anterior vem de C3D_GetProcessingTime/C3D_GetDrawingTime e entra com
 * exo_prof_gpu().
 *
 * CSV: exo_prof_log_start() abre sdmc:/3ds/<app>/prof_NNN.csv (NNN nao
 * sobrescreve sessoes anteriores) e grava uma linha por quadro. As linhas
 * ficam num buffer em RAM e vao para o SD a cada EXO_PROF_ROWS quadros;
 * a coluna "flush" marca o quadro que pagou a escrita, para nao confundir
 * esse pico com custo do jogo.
 */

#include <stdint.h>

#define EXO_PROF_MAX_PHASES   8
#define EXO_PROF_MAX_COUNTERS 6
#define EXO_PROF_ROWS         240

typedef struct ExoProfStats {
	float last_ms[EXO_PROF_MAX_PHASES];
	float avg_ms[EXO_PROF_MAX_PHASES];   /* media movel ~0,5 s */
	float peak_ms[EXO_PROF_MAX_PHASES];  /* pior quadro na janela de 1 s */
	float frame_ms, frame_avg_ms, frame_peak_ms;
	float gpu_proc_ms, gpu_draw_ms;
	uint32_t frame;
	int logging;
	int log_index;                       /* NNN do arquivo atual, -1 sem log */
	uint32_t rows_written;
} ExoProfStats;

/* app_dir: nome da pasta em sdmc:/3ds/ (ex.: "eol", "megaville").
   phase_names/counter_names: rotulos usados no HUD e no cabecalho do CSV. */
void exo_prof_init(const char *app_dir,
                   const char *const *phase_names, int n_phases,
                   const char *const *counter_names, int n_counters);
void exo_prof_shutdown(void);

void exo_prof_frame_begin(void);
void exo_prof_begin(int phase);
void exo_prof_end(int phase);
void exo_prof_counter(int idx, int32_t value);
void exo_prof_gpu(float proc_ms, float draw_ms);
/* texto livre curto gravado na coluna "tag" (curso, piloto...). */
void exo_prof_tag(const char *tag);
void exo_prof_frame_end(void);

int  exo_prof_log_start(void);
void exo_prof_log_stop(void);
int  exo_prof_log_toggle(void);

const ExoProfStats *exo_prof_stats(void);
const char *exo_prof_phase_name(int phase);
int  exo_prof_phase_count(void);

#endif

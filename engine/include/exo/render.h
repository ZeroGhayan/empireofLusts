#ifndef EXO_RENDER_H
#define EXO_RENDER_H

#include "exo/types.h"

#define EXO_TOP_W 400.0f
#define EXO_TOP_H 240.0f
#define EXO_BOT_W 320.0f
#define EXO_BOT_H 240.0f

typedef enum ExoEye {
    EXO_EYE_LEFT  = -1,
	EXO_EYE_RIGHT =  1
} ExoEye;

void  exo_render_begin(void);
void  exo_render_eye(ExoEye eye, uint32_t clear_rgba);
void  exo_render_end(void);

void  exo_render_bottom(uint32_t clear_rgba);
void  exo_bot_rect(float x, float y, float w, float h, uint32_t rgba);
void  exo_text_begin(void);
void  exo_text(float x, float y, float scale, uint32_t rgba, const char *s);
void  exo_top_text(float x, float y, float scale, uint32_t rgba, const char *s);

/* deslocamento em pixels: depth 0 = tela, maior = mais perto */
float exo_parallax(float depth, ExoEye eye);

#endif

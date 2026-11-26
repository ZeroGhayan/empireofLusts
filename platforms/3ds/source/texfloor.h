#ifndef EXO_TEXFLOOR_H
#define EXO_TEXFLOOR_H

#include <citro2d.h>

int  exo_texfloor_init(void);
void exo_texfloor_fini(void);
/* 1x por quadro, antes do primeiro olho: zera o buffer de vertices.
   Os lotes dos dois olhos e de todos os passes ficam em trechos
   diferentes do mesmo buffer, porque a GPU so le tudo no C3D_FrameEnd. */
void exo_texfloor_frame(void);
void exo_texfloor_begin(void);
/* neblina: cor (RGBA8 do citro2d) e liga/desliga para os proximos lotes */
void exo_texfloor_fog_color(u32 rgba);
void exo_texfloor_fog_enable(int on);
/* fator de neblina (0..1) dos 4 cantos do proximo quad (ordem dos cantos
   igual a de sx/sy). Com n != 4 usa fog[0]. */
void exo_texfloor_fog4(const float fog[4]);
void exo_texfloor_fog1(float fog);
void exo_texfloor_quad(C2D_Image img, const float *sx, const float *sy, int n);
void exo_texfloor_end(void);

/* contadores do quadro (perfilador) */
int  exo_texfloor_vertices(void);
int  exo_texfloor_batches(void);
int  exo_texfloor_dropped(void);

#endif

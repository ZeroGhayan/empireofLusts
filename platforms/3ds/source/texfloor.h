#ifndef EXO_TEXFLOOR_H
#define EXO_TEXFLOOR_H

#include <citro2d.h>

int  exo_texfloor_init(void);
void exo_texfloor_fini(void);
void exo_texfloor_begin(void);
void exo_texfloor_quad(C2D_Image img, const float *sx, const float *sy, int n);
void exo_texfloor_end(void);

#endif

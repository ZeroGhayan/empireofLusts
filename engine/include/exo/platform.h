#ifndef EXO_PLATFORM_H
#define EXO_PLATFORM_H

#include "exo/types.h"
#include "exo/input.h"
#include "exo/time.h"
#include "exo/render.h"

bool exo_init(void);
void exo_shutdown(void);
bool exo_frame_begin(void); /* false = sair (HOME / fechar) */
void exo_frame_end(void);

#endif
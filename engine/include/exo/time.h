#ifndef EXO_TIME_H
#define EXO_TIME_H

#include "exo/types.h"

float    exo_dt(void);     /* segundos, limitado a 50 ms */
uint32_t exo_frame(void);

#endif
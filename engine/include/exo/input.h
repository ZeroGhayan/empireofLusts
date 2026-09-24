#ifndef EXO_INPUT_H
#define EXO_INPUT_H

#include "exo/types.h"

enum {
	EXO_BTN_A      = 1u << 0,
	EXO_BTN_B      = 1u << 1,
	EXO_BTN_X      = 1u << 2,
	EXO_BTN_Y      = 1u << 3,
	EXO_BTN_L      = 1u << 4,
	EXO_BTN_R      = 1u << 5,
	EXO_BTN_ZL     = 1u << 6,
	EXO_BTN_ZR     = 1u << 7,
	EXO_BTN_START  = 1u << 8,
	EXO_BTN_SELECT = 1u << 9,
	EXO_BTN_UP     = 1u << 10,
	EXO_BTN_DOWN   = 1u << 11,
	EXO_BTN_LEFT   = 1u << 12,
	EXO_BTN_RIGHT  = 1u << 13
};

typedef struct ExoInput {
	float stick_x;   /* Circle Pad  -1..1 */
	float stick_y;   /* Circle Pad  -1..1  (cima = +) */
	float cstick_x;  /* New 3DS; 0 no 3DS velho */
	float cstick_y;
	float slider_3d; /* 0..1 */
	float gyro_x;    /* gancho; 0 até habilitar */
	float gyro_y;
	float gyro_z;
	uint32_t down;
	uint32_t held;
	uint32_t up;
	bool new_3ds;
    uint16_t touch_x;
	uint16_t touch_y;
	uint8_t  touch_press;
} ExoInput;

const ExoInput *exo_input(void);
bool exo_down(uint32_t mask);
bool exo_held(uint32_t mask);
bool exo_up(uint32_t mask);

#endif
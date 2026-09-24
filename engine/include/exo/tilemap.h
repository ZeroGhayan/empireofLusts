#ifndef EXO_TILEMAP_H
#define EXO_TILEMAP_H

#include "exo/types.h"

#define EXO_ETM_MAGIC    0x314D5445u /* 'ETM1' little-endian */
#define EXO_TILEMAP_MAX  128
#define EXO_TILE_PX      32
#define EXO_TILE_MAX     1024

typedef struct ExoTilemap {
	uint16_t w;
	uint16_t h;
	uint16_t tile_px;
	uint16_t tile_count;
	uint16_t cells[EXO_TILEMAP_MAX * EXO_TILEMAP_MAX];
	bool     loaded;
} ExoTilemap;

void     exo_tilemap_clear(ExoTilemap *m);
void     exo_tilemap_demo(ExoTilemap *m);
bool     exo_tilemap_load(ExoTilemap *m, const void *data, uint32_t size);
uint16_t exo_tilemap_at(const ExoTilemap *m, int x, int y);
bool     exo_tilemap_solid(const ExoTilemap *m, int x, int y);

/* Empacota índice da tile em RGB para o map.png de autoria. */
void     exo_tile_index_rgb(uint16_t idx, uint8_t *r, uint8_t *g, uint8_t *b);
uint16_t exo_tile_rgb_index(uint8_t r, uint8_t g, uint8_t b);

#endif

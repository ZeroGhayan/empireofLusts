#ifndef EXO_TILEMAP_H
#define EXO_TILEMAP_H

#include "exo/types.h"

#define EXO_ETM_MAGIC     0x314D5445u
#define EXO_TILEMAP_MAX   128
#define EXO_TILE_PX       32
#define EXO_TILE_MAX      1024
#define EXO_TILE_WALL     1
#define EXO_TILE_PAD      5
#define EXO_TILE_SPRING   6
#define EXO_HEIGHT_VOID   ((int16_t)-20000)

typedef struct ExoTilemap {
	uint16_t w;
	uint16_t h;
	uint16_t tile_px;
	uint16_t tile_count;
	uint16_t atlas_cols;
	uint16_t cells[EXO_TILEMAP_MAX * EXO_TILEMAP_MAX];
	int16_t  height[EXO_TILEMAP_MAX * EXO_TILEMAP_MAX];
	bool     loaded;
} ExoTilemap;

extern uint8_t exo_sno_cell[EXO_TILEMAP_MAX * EXO_TILEMAP_MAX];
void     exo_sno_ensure(void);

void     exo_tilemap_clear(ExoTilemap *m);
void     exo_tilemap_demo(ExoTilemap *m);
void     exo_tilemap_place_springs(ExoTilemap *m);
void     exo_tilemap_place_pad(ExoTilemap *m);
bool     exo_tilemap_load(ExoTilemap *m, const void *data, uint32_t size);
void     exo_tilemap_from_ids(ExoTilemap *m, const uint8_t *ids,
                             uint16_t w, uint16_t h, uint16_t count, uint16_t cols);
void     exo_tilemap_set_atlas(ExoTilemap *m, uint16_t cols, uint16_t count);
uint16_t exo_tilemap_at(const ExoTilemap *m, int x, int y);
int16_t  exo_tilemap_cell_h(const ExoTilemap *m, int x, int y);
void     exo_tilemap_set_h(ExoTilemap *m, int x, int y, int16_t h);
int      exo_tilemap_walkable(const ExoTilemap *m, int x, int y);
float    exo_tilemap_sample_h(const ExoTilemap *m, float wx, float wz);
bool     exo_tilemap_solid(const ExoTilemap *m, int x, int y);
bool     exo_tilemap_spring(const ExoTilemap *m, int x, int y);
bool     exo_tilemap_pad(const ExoTilemap *m, int x, int y);

void     exo_tile_index_rgb(uint16_t idx, uint8_t *r, uint8_t *g, uint8_t *b);
uint16_t exo_tile_rgb_index(uint8_t r, uint8_t g, uint8_t b);

#endif

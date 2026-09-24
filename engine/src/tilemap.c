#include "exo/tilemap.h"
#include <string.h>

void exo_tilemap_clear(ExoTilemap *m)
{
	memset(m, 0, sizeof(*m));
	m->w = EXO_TILEMAP_MAX;
	m->h = EXO_TILEMAP_MAX;
	m->tile_px = EXO_TILE_PX;
}

void exo_tile_index_rgb(uint16_t idx, uint8_t *r, uint8_t *g, uint8_t *b)
{
	*r = (uint8_t)(idx & 0xFFu);
	*g = (uint8_t)((idx >> 8) & 0xFFu);
	*b = 0x20;
}

uint16_t exo_tile_rgb_index(uint8_t r, uint8_t g, uint8_t b)
{
	(void)b;
	return (uint16_t)r | ((uint16_t)g << 8);
}

uint16_t exo_tilemap_at(const ExoTilemap *m, int x, int y)
{
	if (!m || x < 0 || y < 0 || x >= (int)m->w || y >= (int)m->h)
		return 1; /* fora = barreira */
	return m->cells[y * m->w + x];
}

bool exo_tilemap_solid(const ExoTilemap *m, int x, int y)
{
	uint16_t t = exo_tilemap_at(m, x, y);
	return t == 1;
}

/*
 * Demo 128×128:
 *   0 piso   1 barreira   2 estrada   3 faixa   4 acostamento
 * Estrada em cruz no centro + anel, para testar virada e histerese.
 */
void exo_tilemap_demo(ExoTilemap *m)
{
	int x, z;
	int mid;

	exo_tilemap_clear(m);
	m->tile_count = 5;
	m->loaded = true;
	mid = EXO_TILEMAP_MAX / 2;

	for (z = 0; z < EXO_TILEMAP_MAX; ++z) {
		for (x = 0; x < EXO_TILEMAP_MAX; ++x) {
			uint16_t t = 0;
			int dx = x - mid;
			int dz = z - mid;
			int adx = dx < 0 ? -dx : dx;
			int adz = dz < 0 ? -dz : dz;

			if (x == 0 || z == 0 || x == EXO_TILEMAP_MAX - 1 ||
			    z == EXO_TILEMAP_MAX - 1)
				t = 1;
			else if (adx <= 1 || adz <= 1)
				t = (adx == 0 || adz == 0) ? 3 : 2;
			else if (adx <= 3 || adz <= 3)
				t = 4;
			else if ((adx == 24 || adz == 24) && adx <= 24 && adz <= 24)
				t = 2;
			m->cells[z * m->w + x] = t;
		}
	}
}

bool exo_tilemap_load(ExoTilemap *m, const void *data, uint32_t size)
{
	const uint8_t *p = (const uint8_t *)data;
	uint32_t magic, need;
	uint16_t w, h, tile_px, count;
	uint32_t n, i;

	exo_tilemap_clear(m);
	if (!p || size < 16)
		return false;

	magic = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
	        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
	if (magic != EXO_ETM_MAGIC)
		return false;

	w       = (uint16_t)(p[4] | (p[5] << 8));
	h       = (uint16_t)(p[6] | (p[7] << 8));
	tile_px = (uint16_t)(p[8] | (p[9] << 8));
	count   = (uint16_t)(p[10] | (p[11] << 8));
	/* p[12..15] flags, reservado */

	if (w == 0 || h == 0 || w > EXO_TILEMAP_MAX || h > EXO_TILEMAP_MAX)
		return false;
	if (tile_px == 0)
		tile_px = EXO_TILE_PX;

	n = (uint32_t)w * (uint32_t)h;
	need = 16u + n * 2u;
	if (size < need)
		return false;

	m->w = w;
	m->h = h;
	m->tile_px = tile_px;
	m->tile_count = count;
	for (i = 0; i < n; ++i) {
		const uint8_t *c = p + 16 + i * 2;
		m->cells[i] = (uint16_t)(c[0] | (c[1] << 8));
	}
	m->loaded = true;
	return true;
}

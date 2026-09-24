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
		return EXO_TILE_WALL;
	return m->cells[y * m->w + x];
}

bool exo_tilemap_solid(const ExoTilemap *m, int x, int y)
{
	return exo_tilemap_at(m, x, y) == EXO_TILE_WALL;
}

bool exo_tilemap_spring(const ExoTilemap *m, int x, int y)
{
	return exo_tilemap_at(m, x, y) == EXO_TILE_SPRING;
}

bool exo_tilemap_pad(const ExoTilemap *m, int x, int y)
{
	return exo_tilemap_at(m, x, y) == EXO_TILE_PAD;
}

void exo_tilemap_place_springs(ExoTilemap *m)
{
	int mid;
	int spots[4][2];
	int i;

	if (!m || m->w < 16 || m->h < 16)
		return;
	mid = (int)m->w / 2;
	spots[0][0] = mid;     spots[0][1] = mid - 6;
	spots[1][0] = mid;     spots[1][1] = mid + 6;
	spots[2][0] = mid - 6; spots[2][1] = mid;
	spots[3][0] = mid + 6; spots[3][1] = mid;
	for (i = 0; i < 4; ++i) {
		int x = spots[i][0];
		int z = spots[i][1];
		if (x > 0 && z > 0 && x < (int)m->w - 1 && z < (int)m->h - 1)
			m->cells[z * m->w + x] = EXO_TILE_SPRING;
	}
	if (m->tile_count < 7)
		m->tile_count = 7;
}

void exo_tilemap_place_pad(ExoTilemap *m)
{
	int mid, x, z, dx, dz;

	if (!m || m->w < 20 || m->h < 20)
		return;
	mid = (int)m->w / 2;
	/* pad 2x2 roxo a norte da cruz, fim do percurso */
	for (dz = 0; dz < 2; ++dz) {
		for (dx = 0; dx < 2; ++dx) {
			x = mid + dx;
			z = mid + 18 + dz;
			if (x > 0 && z > 0 && x < (int)m->w - 1 && z < (int)m->h - 1)
				m->cells[z * m->w + x] = EXO_TILE_PAD;
		}
	}
	if (m->tile_count < 7)
		m->tile_count = 7;
}

void exo_tilemap_demo(ExoTilemap *m)
{
	int x, z;
	int mid;

	exo_tilemap_clear(m);
	m->tile_count = 7;
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
				t = EXO_TILE_WALL;
			else if (adx <= 1 || adz <= 1)
				t = (adx == 0 || adz == 0) ? 3 : 2;
			else if (adx <= 3 || adz <= 3)
				t = 4;
			else if ((adx == 24 || adz == 24) && adx <= 24 && adz <= 24)
				t = 2;
			m->cells[z * m->w + x] = t;
		}
	}
	exo_tilemap_place_springs(m);
	exo_tilemap_place_pad(m);
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
	exo_tilemap_place_springs(m);
	exo_tilemap_place_pad(m);
	return true;
}

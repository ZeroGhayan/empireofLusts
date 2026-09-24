#include "exo/tilemap.h"
#include <string.h>

static uint16_t guess_cols(uint16_t count)
{
	uint16_t c;

	if (count <= 1)
		return 1;
	c = 1;
	while ((uint32_t)c * (uint32_t)c < (uint32_t)count)
		c++;
	return c;
}

void exo_tilemap_clear(ExoTilemap *m)
{
	memset(m, 0, sizeof(*m));
	m->w = EXO_TILEMAP_MAX;
	m->h = EXO_TILEMAP_MAX;
	m->tile_px = EXO_TILE_PX;
	m->atlas_cols = 8;
}

void exo_tilemap_set_atlas(ExoTilemap *m, uint16_t cols, uint16_t count)
{
	if (!m)
		return;
	if (count)
		m->tile_count = count;
	if (cols)
		m->atlas_cols = cols;
	else if (m->tile_count)
		m->atlas_cols = guess_cols(m->tile_count);
}

void exo_tilemap_from_ids(ExoTilemap *m, const uint8_t *ids,
                          uint16_t w, uint16_t h, uint16_t count, uint16_t cols)
{
	uint32_t n, i;

	exo_tilemap_clear(m);
	if (!ids || w == 0 || h == 0 || w > EXO_TILEMAP_MAX || h > EXO_TILEMAP_MAX)
		return;
	m->w = w;
	m->h = h;
	m->tile_px = EXO_TILE_PX;
	m->tile_count = count ? count : 1;
	m->atlas_cols = cols ? cols : guess_cols(m->tile_count);
	n = (uint32_t)w * (uint32_t)h;
	for (i = 0; i < n; ++i)
		m->cells[i] = ids[i];
	m->loaded = true;
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

int16_t exo_tilemap_cell_h(const ExoTilemap *m, int x, int y)
{
	if (!m || x < 0 || y < 0 || x >= (int)m->w || y >= (int)m->h)
		return EXO_HEIGHT_VOID;
	return m->height[y * m->w + x];
}

void exo_tilemap_set_h(ExoTilemap *m, int x, int y, int16_t h)
{
	if (!m || x < 0 || y < 0 || x >= (int)m->w || y >= (int)m->h)
		return;
	m->height[y * m->w + x] = h;
}

int exo_tilemap_walkable(const ExoTilemap *m, int x, int y)
{
	return exo_tilemap_cell_h(m, x, y) > (EXO_HEIGHT_VOID / 2);
}

float exo_tilemap_sample_h(const ExoTilemap *m, float wx, float wz)
{
	float gx, gz, fx, fz;
	int x0, z0;
	float h00, h10, h01, h11;

	gx = wx / 32.0f;
	gz = wz / 32.0f;
	x0 = (int)gx;
	z0 = (int)gz;
	if (gx < 0.0f) x0 = (int)gx - 1;
	if (gz < 0.0f) z0 = (int)gz - 1;
	fx = gx - (float)x0;
	fz = gz - (float)z0;
	if (!exo_tilemap_walkable(m, x0, z0))
		return (float)EXO_HEIGHT_VOID;
	h00 = (float)exo_tilemap_cell_h(m, x0, z0);
	h10 = exo_tilemap_walkable(m, x0 + 1, z0) ? (float)exo_tilemap_cell_h(m, x0 + 1, z0) : h00;
	h01 = exo_tilemap_walkable(m, x0, z0 + 1) ? (float)exo_tilemap_cell_h(m, x0, z0 + 1) : h00;
	h11 = exo_tilemap_walkable(m, x0 + 1, z0 + 1) ? (float)exo_tilemap_cell_h(m, x0 + 1, z0 + 1) : h00;
	return h00 * (1.0f - fx) * (1.0f - fz) +
	       h10 * fx * (1.0f - fz) +
	       h01 * (1.0f - fx) * fz +
	       h11 * fx * fz;
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
	m->atlas_cols = 8;
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
	uint32_t magic;
	uint16_t w, h, tile_px, count, flags;
	uint32_t n, i, have;

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
	flags   = (uint16_t)(p[12] | (p[13] << 8));

	if (w == 0 || h == 0 || w > EXO_TILEMAP_MAX || h > EXO_TILEMAP_MAX)
		return false;
	if (tile_px == 0)
		tile_px = EXO_TILE_PX;

	n = (uint32_t)w * (uint32_t)h;
	have = (size - 16u) / 2u;
	if (have < n)
		n = have;

	m->w = w;
	m->h = h;
	m->tile_px = tile_px;
	m->tile_count = count;
	m->atlas_cols = flags ? flags : guess_cols(count ? count : 1);
	for (i = 0; i < n; ++i) {
		const uint8_t *c = p + 16 + i * 2;
		m->cells[i] = (uint16_t)(c[0] | (c[1] << 8));
	}
	m->loaded = true;
	return true;
}

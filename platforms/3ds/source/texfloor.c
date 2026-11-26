#include "texfloor.h"
#include "texfloor_shbin.h"

#include <3ds.h>
#include <citro3d.h>
#include <citro2d.h>
#include <string.h>
#include <stdlib.h>

/* 2 olhos x (nuvem + piso) x ~720 tiles x ate 5 quads x 6 vertices cabia
   mal em 720*6; agora o buffer e do quadro inteiro. 24576*24 B = 576 KiB
   de linear heap. */
#define MAX_VTX 24576

typedef struct {
	float x, y, z;
	float u, v;
	float fog;
} FloorVtx;

static shaderProgram_s s_prog;
static DVLB_s *s_dvlb;
static int s_uLoc;
static C3D_Mtx s_proj;
static FloorVtx *s_vtx;
static int s_n;         /* proximo vertice livre no quadro */
static int s_start;     /* inicio do lote atual */
static C3D_Tex *s_tex;
static int s_ready;
static float s_fog[4];
static int s_fog_on = 1;
static u32 s_fog_rgba = 0xFF826440u;
static int s_batches, s_dropped;

int exo_texfloor_init(void)
{
	s_dvlb = DVLB_ParseFile((u32 *)texfloor_shbin, texfloor_shbin_size);
	if (!s_dvlb)
		return 0;
	shaderProgramInit(&s_prog);
	if (shaderProgramSetVsh(&s_prog, &s_dvlb->DVLE[0]) != 0)
		return 0;
	s_uLoc = shaderInstanceGetUniformLocation(s_prog.vertexShader, "projection");
	Mtx_OrthoTilt(&s_proj, 0.0f, 400.0f, 240.0f, 0.0f, 0.0f, 1.0f, true);
	s_vtx = (FloorVtx *)linearAlloc(sizeof(FloorVtx) * MAX_VTX);
	if (!s_vtx)
		return 0;
	s_ready = 1;
	s_n = 0;
	s_start = 0;
	s_tex = NULL;
	return 1;
}

void exo_texfloor_fini(void)
{
	if (s_vtx) {
		linearFree(s_vtx);
		s_vtx = NULL;
	}
	if (s_ready) {
		shaderProgramFree(&s_prog);
		DVLB_Free(s_dvlb);
		s_dvlb = NULL;
	}
	s_ready = 0;
}

void exo_texfloor_frame(void)
{
	s_n = 0;
	s_start = 0;
	s_tex = NULL;
	s_batches = 0;
	s_dropped = 0;
}

void exo_texfloor_begin(void)
{
	s_start = s_n;
	s_tex = NULL;
	s_fog[0] = s_fog[1] = s_fog[2] = s_fog[3] = 0.0f;
}

void exo_texfloor_fog_color(u32 rgba) { s_fog_rgba = rgba; }
void exo_texfloor_fog_enable(int on) { s_fog_on = on; }

void exo_texfloor_fog4(const float fog[4])
{
	int i;
	for (i = 0; i < 4; ++i)
		s_fog[i] = fog[i] < 0.0f ? 0.0f : (fog[i] > 1.0f ? 1.0f : fog[i]);
}

void exo_texfloor_fog1(float fog)
{
	float f4[4] = { fog, fog, fog, fog };
	exo_texfloor_fog4(f4);
}

static void push_tri(float x0, float y0, float u0, float v0, float f0,
                     float x1, float y1, float u1, float v1, float f1,
                     float x2, float y2, float u2, float v2, float f2)
{
	FloorVtx *p;
	if (!s_vtx || s_n + 3 > MAX_VTX) {
		s_dropped++;
		return;
	}
	p = &s_vtx[s_n];
	p[0].x = x0; p[0].y = y0; p[0].z = 0.31f; p[0].u = u0; p[0].v = v0; p[0].fog = f0;
	p[1].x = x1; p[1].y = y1; p[1].z = 0.31f; p[1].u = u1; p[1].v = v1; p[1].fog = f1;
	p[2].x = x2; p[2].y = y2; p[2].z = 0.31f; p[2].u = u2; p[2].v = v2; p[2].fog = f2;
	s_n += 3;
}

static void flush_batch(void)
{
	C3D_AttrInfo *attr;
	C3D_BufInfo *buf;
	C3D_TexEnv *env;
	int count = s_n - s_start;

	if (!s_ready || !s_vtx || count < 3 || !s_tex) {
		s_start = s_n;
		return;
	}

	/* a GPU le direto da RAM: tira os vertices novos do cache da CPU */
	GSPGPU_FlushDataCache(s_vtx + s_start, (u32)count * sizeof(FloorVtx));
	C2D_Flush();
	C3D_BindProgram(&s_prog);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_uLoc, &s_proj);

	attr = C3D_GetAttrInfo();
	AttrInfo_Init(attr);
	AttrInfo_AddLoader(attr, 0, GPU_FLOAT, 3);
	AttrInfo_AddLoader(attr, 1, GPU_FLOAT, 2);
	AttrInfo_AddLoader(attr, 2, GPU_FLOAT, 1);

	buf = C3D_GetBufInfo();
	BufInfo_Init(buf);
	BufInfo_Add(buf, s_vtx + s_start, sizeof(FloorVtx), 3, 0x210);

	C3D_TexBind(0, s_tex);
	env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	if (s_fog_on) {
		/* rgb = neblina*f + textura*(1-f); alfa = alfa da textura */
		C3D_TexEnvSrc(env, C3D_RGB, GPU_CONSTANT, GPU_TEXTURE0, GPU_PRIMARY_COLOR);
		C3D_TexEnvFunc(env, C3D_RGB, GPU_INTERPOLATE);
		C3D_TexEnvColor(env, s_fog_rgba);
		C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, 0, 0);
		C3D_TexEnvFunc(env, C3D_Alpha, GPU_REPLACE);
	} else {
		C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, 0, 0);
		C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
	}
	/* estagios 1 e 2 ficam com a config do citro2d (tinta de texto);
	   aqui eles so precisam deixar passar o resultado do estagio 0 */
	C3D_TexEnvInit(C3D_GetTexEnv(1));
	C3D_TexEnvInit(C3D_GetTexEnv(2));

	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);
	C3D_CullFace(GPU_CULL_NONE);
	C3D_DrawArrays(GPU_TRIANGLES, 0, count);
	s_batches++;

	s_start = s_n;
	C2D_Prepare();
}

void exo_texfloor_quad(C2D_Image img, const float *sx, const float *sy, int n)
{
	const Tex3DS_SubTexture *st;
	float u0, v0, u1, v1;
	const float *g = s_fog;

	if (!s_ready || !s_vtx || !img.tex || !img.subtex || n < 3)
		return;
	/* antes o lote inteiro era desenhado com a ULTIMA textura vista; agora
	   troca de textura fecha o lote atual (nuvem x tiles x tiles2). */
	if (s_tex && img.tex != s_tex)
		flush_batch();
	st = img.subtex;
	u0 = st->left;
	v0 = st->top;
	u1 = st->right;
	v1 = st->bottom;
	s_tex = img.tex;

	if (n == 4) {
		push_tri(sx[0], sy[0], u0, v0, g[0],
		         sx[1], sy[1], u1, v0, g[1],
		         sx[2], sy[2], u1, v1, g[2]);
		push_tri(sx[0], sy[0], u0, v0, g[0],
		         sx[2], sy[2], u1, v1, g[2],
		         sx[3], sy[3], u0, v1, g[3]);
		return;
	}
	{
		int i;
		for (i = 1; i < n - 1; ++i)
			push_tri(sx[0], sy[0], u0, v0, g[0],
			         sx[i], sy[i], u1, v0, g[0],
			         sx[i + 1], sy[i + 1], u1, v1, g[0]);
	}
}

void exo_texfloor_end(void)
{
	flush_batch();
	s_tex = NULL;
}

int exo_texfloor_vertices(void) { return s_n; }
int exo_texfloor_batches(void) { return s_batches; }
int exo_texfloor_dropped(void) { return s_dropped; }

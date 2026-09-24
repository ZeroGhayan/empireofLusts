#include "texfloor.h"
#include "texfloor_shbin.h"

#include <3ds.h>
#include <citro3d.h>
#include <citro2d.h>
#include <string.h>
#include <stdlib.h>

#define MAX_VTX (720 * 6)

typedef struct {
	float x, y, z;
	float u, v;
} FloorVtx;

static shaderProgram_s s_prog;
static DVLB_s *s_dvlb;
static int s_uLoc;
static C3D_Mtx s_proj;
static FloorVtx *s_vtx;
static int s_n;
static C3D_Tex *s_tex;
static int s_ready;

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

void exo_texfloor_begin(void)
{
	s_n = 0;
	s_tex = NULL;
}

static void push_tri(float x0, float y0, float u0, float v0,
                     float x1, float y1, float u1, float v1,
                     float x2, float y2, float u2, float v2)
{
	FloorVtx *p;
	if (!s_vtx || s_n + 3 > MAX_VTX)
		return;
	p = &s_vtx[s_n];
	p[0].x = x0; p[0].y = y0; p[0].z = 0.31f; p[0].u = u0; p[0].v = v0;
	p[1].x = x1; p[1].y = y1; p[1].z = 0.31f; p[1].u = u1; p[1].v = v1;
	p[2].x = x2; p[2].y = y2; p[2].z = 0.31f; p[2].u = u2; p[2].v = v2;
	s_n += 3;
}

void exo_texfloor_quad(C2D_Image img, const float *sx, const float *sy, int n)
{
	const Tex3DS_SubTexture *st;
	float u0, v0, u1, v1;

	if (!s_ready || !s_vtx || !img.tex || !img.subtex || n < 3)
		return;
	st = img.subtex;
	u0 = st->left;
	v0 = st->top;
	u1 = st->right;
	v1 = st->bottom;
	s_tex = img.tex;

	if (n == 4) {
		push_tri(sx[0], sy[0], u0, v0,
		         sx[1], sy[1], u1, v0,
		         sx[2], sy[2], u1, v1);
		push_tri(sx[0], sy[0], u0, v0,
		         sx[2], sy[2], u1, v1,
		         sx[3], sy[3], u0, v1);
		return;
	}
	{
		int i;
		for (i = 1; i < n - 1; ++i)
			push_tri(sx[0], sy[0], u0, v0,
			         sx[i], sy[i], u1, v0,
			         sx[i + 1], sy[i + 1], u1, v1);
	}
}

void exo_texfloor_end(void)
{
	C3D_AttrInfo *attr;
	C3D_BufInfo *buf;
	C3D_TexEnv *env;

	if (!s_ready || !s_vtx || s_n < 3 || !s_tex)
		return;

	C2D_Flush();
	C3D_BindProgram(&s_prog);
	C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, s_uLoc, &s_proj);

	attr = C3D_GetAttrInfo();
	AttrInfo_Init(attr);
	AttrInfo_AddLoader(attr, 0, GPU_FLOAT, 3);
	AttrInfo_AddLoader(attr, 1, GPU_FLOAT, 2);

	buf = C3D_GetBufInfo();
	BufInfo_Init(buf);
	BufInfo_Add(buf, s_vtx, sizeof(FloorVtx), 2, 0x10);

	C3D_TexBind(0, s_tex);
	env = C3D_GetTexEnv(0);
	C3D_TexEnvInit(env);
	C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, 0, 0);
	C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);

	C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_ALL);
	C3D_CullFace(GPU_CULL_NONE);
	C3D_DrawArrays(GPU_TRIANGLES, 0, s_n);

	s_n = 0;
	C2D_Prepare();
}

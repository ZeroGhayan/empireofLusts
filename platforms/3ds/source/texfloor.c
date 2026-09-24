#include "texfloor.h"
#include "texfloor_shbin.h"

#include <3ds.h>
#include <citro3d.h>
#include <citro2d.h>
#include <string.h>

#define MAX_VTX (720 * 6)

typedef struct {
	float x, y, z;
	float u, v;
} FloorVtx;

static shaderProgram_s s_prog;
static DVLB_s *s_dvlb;
static int s_uLoc;
static C3D_Mtx s_proj;
static FloorVtx s_vtx[MAX_VTX];
static int s_n;
static C3D_Tex *s_tex;
static int s_ready;

int exo_texfloor_init(void)
{
	C3D_AttrInfo *attr;

	s_dvlb = DVLB_ParseFile((u32 *)texfloor_shbin, texfloor_shbin_size);
	if (!s_dvlb)
		return 0;
	shaderProgramInit(&s_prog);
	shaderProgramSetVsh(&s_prog, &s_dvlb->DVLE[0]);
	s_uLoc = shaderInstanceGetUniformLocation(s_prog.vertexShader, "projection");
	Mtx_OrthoTilt(&s_proj, 0.0f, 400.0f, 240.0f, 0.0f, 0.0f, 1.0f, true);

	attr = C3D_GetAttrInfo();
	(void)attr;
	s_ready = 1;
	s_n = 0;
	s_tex = NULL;
	return 1;
}

void exo_texfloor_fini(void)
{
	if (!s_ready)
		return;
	shaderProgramFree(&s_prog);
	DVLB_Free(s_dvlb);
	s_dvlb = NULL;
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
	if (s_n + 3 > MAX_VTX)
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
	int i;

	if (!s_ready || !img.tex || !img.subtex || n < 3)
		return;
	st = img.subtex;
	u0 = st->left;
	v0 = st->top;
	u1 = st->right;
	v1 = st->bottom;
	s_tex = img.tex;

	/* fan: vertex 0 is UV origin; remaining verts walk the clip ring.
	   For a regular 4-corner tile the UVs are the unit square. */
	if (n == 4) {
		float uu[4] = { u0, u1, u1, u0 };
		float vv[4] = { v0, v0, v1, v1 };
		push_tri(sx[0], sy[0], uu[0], vv[0],
		         sx[1], sy[1], uu[1], vv[1],
		         sx[2], sy[2], uu[2], vv[2]);
		push_tri(sx[0], sy[0], uu[0], vv[0],
		         sx[2], sy[2], uu[2], vv[2],
		         sx[3], sy[3], uu[3], vv[3]);
		return;
	}
	for (i = 1; i < n - 1; ++i) {
		float t0 = 0.0f;
		float t1 = (float)i / (float)(n - 1);
		float t2 = (float)(i + 1) / (float)(n - 1);
		push_tri(sx[0], sy[0], u0 + (u1 - u0) * t0, v0 + (v1 - v0) * t0,
		         sx[i], sy[i], u0 + (u1 - u0) * t1, v0 + (v1 - v0) * t1,
		         sx[i + 1], sy[i + 1], u0 + (u1 - u0) * t2, v0 + (v1 - v0) * t2);
	}
}

void exo_texfloor_end(void)
{
	C3D_AttrInfo *attr;
	C3D_BufInfo *buf;
	C3D_TexEnv *env;

	if (!s_ready || s_n < 3 || !s_tex)
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

	C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
	C3D_CullFace(GPU_CULL_NONE);
	C3D_DrawArrays(GPU_TRIANGLES, 0, s_n);

	s_n = 0;
	C2D_Prepare();
}

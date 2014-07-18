#include <stdlib.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>
#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_io.h"

#include "i830_reg.h"
#include "rendercopy.h"

#define TB0C_LAST_STAGE	(1 << 31)
#define TB0C_RESULT_SCALE_1X		(0 << 29)
#define TB0C_RESULT_SCALE_2X		(1 << 29)
#define TB0C_RESULT_SCALE_4X		(2 << 29)
#define TB0C_OP_ARG1			(1 << 25)
#define TB0C_OP_MODULE			(3 << 25)
#define TB0C_OUTPUT_WRITE_CURRENT	(0 << 24)
#define TB0C_OUTPUT_WRITE_ACCUM		(1 << 24)
#define TB0C_ARG3_REPLICATE_ALPHA 	(1<<23)
#define TB0C_ARG3_INVERT		(1<<22)
#define TB0C_ARG3_SEL_XXX
#define TB0C_ARG2_REPLICATE_ALPHA 	(1<<17)
#define TB0C_ARG2_INVERT		(1<<16)
#define TB0C_ARG2_SEL_ONE		(0 << 12)
#define TB0C_ARG2_SEL_FACTOR		(1 << 12)
#define TB0C_ARG2_SEL_TEXEL0		(6 << 12)
#define TB0C_ARG2_SEL_TEXEL1		(7 << 12)
#define TB0C_ARG2_SEL_TEXEL2		(8 << 12)
#define TB0C_ARG2_SEL_TEXEL3		(9 << 12)
#define TB0C_ARG1_REPLICATE_ALPHA 	(1<<11)
#define TB0C_ARG1_INVERT		(1<<10)
#define TB0C_ARG1_SEL_ONE		(0 << 6)
#define TB0C_ARG1_SEL_TEXEL0		(6 << 6)
#define TB0C_ARG1_SEL_TEXEL1		(7 << 6)
#define TB0C_ARG1_SEL_TEXEL2		(8 << 6)
#define TB0C_ARG1_SEL_TEXEL3		(9 << 6)
#define TB0C_ARG0_REPLICATE_ALPHA 	(1<<5)
#define TB0C_ARG0_SEL_XXX

#define TB0A_CTR_STAGE_ENABLE 		(1<<31)
#define TB0A_RESULT_SCALE_1X		(0 << 29)
#define TB0A_RESULT_SCALE_2X		(1 << 29)
#define TB0A_RESULT_SCALE_4X		(2 << 29)
#define TB0A_OP_ARG1			(1 << 25)
#define TB0A_OP_MODULE			(3 << 25)
#define TB0A_OUTPUT_WRITE_CURRENT	(0<<24)
#define TB0A_OUTPUT_WRITE_ACCUM		(1<<24)
#define TB0A_CTR_STAGE_SEL_BITS_XXX
#define TB0A_ARG3_SEL_XXX
#define TB0A_ARG3_INVERT		(1<<17)
#define TB0A_ARG2_INVERT		(1<<16)
#define TB0A_ARG2_SEL_ONE		(0 << 12)
#define TB0A_ARG2_SEL_TEXEL0		(6 << 12)
#define TB0A_ARG2_SEL_TEXEL1		(7 << 12)
#define TB0A_ARG2_SEL_TEXEL2		(8 << 12)
#define TB0A_ARG2_SEL_TEXEL3		(9 << 12)
#define TB0A_ARG1_INVERT		(1<<10)
#define TB0A_ARG1_SEL_ONE		(0 << 6)
#define TB0A_ARG1_SEL_TEXEL0		(6 << 6)
#define TB0A_ARG1_SEL_TEXEL1		(7 << 6)
#define TB0A_ARG1_SEL_TEXEL2		(8 << 6)
#define TB0A_ARG1_SEL_TEXEL3		(9 << 6)


static void gen2_emit_invariant(struct intel_batchbuffer *batch)
{
	int i;

	for (i = 0; i < 4; i++) {
		OUT_BATCH(_3DSTATE_MAP_CUBE | MAP_UNIT(i));
		OUT_BATCH(_3DSTATE_MAP_TEX_STREAM_CMD | MAP_UNIT(i) |
			  DISABLE_TEX_STREAM_BUMP |
			  ENABLE_TEX_STREAM_COORD_SET | TEX_STREAM_COORD_SET(i) |
			  ENABLE_TEX_STREAM_MAP_IDX | TEX_STREAM_MAP_IDX(i));
		OUT_BATCH(_3DSTATE_MAP_COORD_TRANSFORM);
		OUT_BATCH(DISABLE_TEX_TRANSFORM | TEXTURE_SET(i));
	}

	OUT_BATCH(_3DSTATE_MAP_COORD_SETBIND_CMD);
	OUT_BATCH(TEXBIND_SET3(TEXCOORDSRC_VTXSET_3) |
		  TEXBIND_SET2(TEXCOORDSRC_VTXSET_2) |
		  TEXBIND_SET1(TEXCOORDSRC_VTXSET_1) |
		  TEXBIND_SET0(TEXCOORDSRC_VTXSET_0));

	OUT_BATCH(_3DSTATE_SCISSOR_ENABLE_CMD | DISABLE_SCISSOR_RECT);

	OUT_BATCH(_3DSTATE_VERTEX_TRANSFORM);
	OUT_BATCH(DISABLE_VIEWPORT_TRANSFORM | DISABLE_PERSPECTIVE_DIVIDE);

	OUT_BATCH(_3DSTATE_W_STATE_CMD);
	OUT_BATCH(MAGIC_W_STATE_DWORD1);
	OUT_BATCH(0x3f800000 /* 1.0 in IEEE float */ );

	OUT_BATCH(_3DSTATE_INDPT_ALPHA_BLEND_CMD |
		  DISABLE_INDPT_ALPHA_BLEND |
		  ENABLE_ALPHA_BLENDFUNC | ABLENDFUNC_ADD);

	OUT_BATCH(_3DSTATE_CONST_BLEND_COLOR_CMD);
	OUT_BATCH(0);

	OUT_BATCH(_3DSTATE_MODES_1_CMD |
		  ENABLE_COLR_BLND_FUNC | BLENDFUNC_ADD |
		  ENABLE_SRC_BLND_FACTOR | SRC_BLND_FACT(BLENDFACTOR_ONE) |
		  ENABLE_DST_BLND_FACTOR | DST_BLND_FACT(BLENDFACTOR_ZERO));

	OUT_BATCH(_3DSTATE_ENABLES_1_CMD |
		  DISABLE_LOGIC_OP |
		  DISABLE_STENCIL_TEST |
		  DISABLE_DEPTH_BIAS |
		  DISABLE_SPEC_ADD |
		  DISABLE_FOG |
		  DISABLE_ALPHA_TEST |
		  DISABLE_DEPTH_TEST |
		  ENABLE_COLOR_BLEND);

	OUT_BATCH(_3DSTATE_ENABLES_2_CMD |
		  DISABLE_STENCIL_WRITE |
		  DISABLE_DITHER |
		  DISABLE_DEPTH_WRITE |
		  ENABLE_COLOR_MASK |
		  ENABLE_COLOR_WRITE |
		  ENABLE_TEX_CACHE);
}

static void gen2_emit_target(struct intel_batchbuffer *batch,
			     struct igt_buf *dst)
{
	uint32_t tiling;

	tiling = 0;
	if (dst->tiling != I915_TILING_NONE)
		tiling = BUF_3D_TILED_SURFACE;
	if (dst->tiling == I915_TILING_Y)
		tiling |= BUF_3D_TILE_WALK_Y;

	OUT_BATCH(_3DSTATE_BUF_INFO_CMD);
	OUT_BATCH(BUF_3D_ID_COLOR_BACK | tiling | BUF_3D_PITCH(dst->stride));
	OUT_RELOC(dst->bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);

	OUT_BATCH(_3DSTATE_DST_BUF_VARS_CMD);
	OUT_BATCH(COLR_BUF_ARGB8888 |
		  DSTORG_HORT_BIAS(0x8) |
		  DSTORG_VERT_BIAS(0x8));

	OUT_BATCH(_3DSTATE_DRAW_RECT_CMD);
	OUT_BATCH(0);
	OUT_BATCH(0);		/* ymin, xmin */
	OUT_BATCH(DRAW_YMAX(igt_buf_height(dst) - 1) |
		  DRAW_XMAX(igt_buf_width(dst) - 1));
	OUT_BATCH(0);		/* yorig, xorig */
}

static void gen2_emit_texture(struct intel_batchbuffer *batch,
			      struct igt_buf *src,
			      int unit)
{
	uint32_t tiling;

	tiling = 0;
	if (src->tiling != I915_TILING_NONE)
		tiling = TM0S1_TILED_SURFACE;
	if (src->tiling == I915_TILING_Y)
		tiling |= TM0S1_TILE_WALK;

	OUT_BATCH(_3DSTATE_LOAD_STATE_IMMEDIATE_2 | LOAD_TEXTURE_MAP(unit) | 4);
	OUT_RELOC(src->bo, I915_GEM_DOMAIN_SAMPLER, 0, 0);
	OUT_BATCH((igt_buf_height(src) - 1) << TM0S1_HEIGHT_SHIFT |
		  (igt_buf_width(src) - 1) << TM0S1_WIDTH_SHIFT |
		  MAPSURF_32BIT | MT_32BIT_ARGB8888 | tiling);
	OUT_BATCH((src->stride / 4 - 1) << TM0S2_PITCH_SHIFT | TM0S2_MAP_2D);
	OUT_BATCH(FILTER_NEAREST << TM0S3_MAG_FILTER_SHIFT |
		  FILTER_NEAREST << TM0S3_MIN_FILTER_SHIFT |
		  MIPFILTER_NONE << TM0S3_MIP_FILTER_SHIFT);
	OUT_BATCH(0);	/* default color */

	OUT_BATCH(_3DSTATE_MAP_COORD_SET_CMD | TEXCOORD_SET(unit) |
		  ENABLE_TEXCOORD_PARAMS | TEXCOORDS_ARE_NORMAL |
		  TEXCOORDTYPE_CARTESIAN |
		  ENABLE_ADDR_V_CNTL | TEXCOORD_ADDR_V_MODE(TEXCOORDMODE_CLAMP_BORDER) |
		  ENABLE_ADDR_U_CNTL | TEXCOORD_ADDR_U_MODE(TEXCOORDMODE_CLAMP_BORDER));
}

static void gen2_emit_copy_pipeline(struct intel_batchbuffer *batch)
{
	OUT_BATCH(_3DSTATE_INDPT_ALPHA_BLEND_CMD | DISABLE_INDPT_ALPHA_BLEND);
	OUT_BATCH(_3DSTATE_ENABLES_1_CMD | DISABLE_LOGIC_OP |
		  DISABLE_STENCIL_TEST | DISABLE_DEPTH_BIAS |
		  DISABLE_SPEC_ADD | DISABLE_FOG | DISABLE_ALPHA_TEST |
		  DISABLE_COLOR_BLEND | DISABLE_DEPTH_TEST);

	OUT_BATCH(_3DSTATE_LOAD_STATE_IMMEDIATE_2 |
		  LOAD_TEXTURE_BLEND_STAGE(0) | 1);
	OUT_BATCH(TB0C_LAST_STAGE | TB0C_RESULT_SCALE_1X |
		  TB0C_OUTPUT_WRITE_CURRENT |
		  TB0C_OP_ARG1 | TB0C_ARG1_SEL_TEXEL0);
	OUT_BATCH(TB0A_RESULT_SCALE_1X | TB0A_OUTPUT_WRITE_CURRENT |
		  TB0A_OP_ARG1 | TB0A_ARG1_SEL_TEXEL0);
}

void gen2_render_copyfunc(struct intel_batchbuffer *batch,
			  drm_intel_context *context,
			  struct igt_buf *src, unsigned src_x, unsigned src_y,
			  unsigned width, unsigned height,
			  struct igt_buf *dst, unsigned dst_x, unsigned dst_y)
{
	gen2_emit_invariant(batch);
	gen2_emit_copy_pipeline(batch);

	gen2_emit_target(batch, dst);
	gen2_emit_texture(batch, src, 0);

	OUT_BATCH(_3DSTATE_LOAD_STATE_IMMEDIATE_1 |
		  I1_LOAD_S(2) | I1_LOAD_S(3) | I1_LOAD_S(8) | 2);
	OUT_BATCH(1<<12);
	OUT_BATCH(S3_CULLMODE_NONE | S3_VERTEXHAS_XY);
	OUT_BATCH(S8_ENABLE_COLOR_BUFFER_WRITE);

	OUT_BATCH(_3DSTATE_VERTEX_FORMAT_2_CMD | TEXCOORDFMT_2D << 0);

	OUT_BATCH(PRIM3D_INLINE | PRIM3D_RECTLIST | (3*4 -1));
	emit_vertex(batch, dst_x + width);
	emit_vertex(batch, dst_y + height);
	emit_vertex_normalized(batch, src_x + width, igt_buf_width(src));
	emit_vertex_normalized(batch, src_y + height, igt_buf_height(src));

	emit_vertex(batch, dst_x);
	emit_vertex(batch, dst_y + height);
	emit_vertex_normalized(batch, src_x, igt_buf_width(src));
	emit_vertex_normalized(batch, src_y + height, igt_buf_height(src));

	emit_vertex(batch, dst_x);
	emit_vertex(batch, dst_y);
	emit_vertex_normalized(batch, src_x, igt_buf_width(src));
	emit_vertex_normalized(batch, src_y, igt_buf_height(src));

	intel_batchbuffer_flush(batch);
}

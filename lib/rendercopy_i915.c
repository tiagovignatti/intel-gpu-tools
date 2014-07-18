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

#include "i915_reg.h"
#include "i915_3d.h"
#include "rendercopy.h"

void gen3_render_copyfunc(struct intel_batchbuffer *batch,
			  drm_intel_context *context,
			  struct igt_buf *src, unsigned src_x, unsigned src_y,
			  unsigned width, unsigned height,
			  struct igt_buf *dst, unsigned dst_x, unsigned dst_y)
{
	/* invariant state */
	{
		OUT_BATCH(_3DSTATE_AA_CMD |
			  AA_LINE_ECAAR_WIDTH_ENABLE |
			  AA_LINE_ECAAR_WIDTH_1_0 |
			  AA_LINE_REGION_WIDTH_ENABLE | AA_LINE_REGION_WIDTH_1_0);
		OUT_BATCH(_3DSTATE_INDEPENDENT_ALPHA_BLEND_CMD |
			  IAB_MODIFY_ENABLE |
			  IAB_MODIFY_FUNC | (BLENDFUNC_ADD << IAB_FUNC_SHIFT) |
			  IAB_MODIFY_SRC_FACTOR | (BLENDFACT_ONE <<
						   IAB_SRC_FACTOR_SHIFT) |
			  IAB_MODIFY_DST_FACTOR | (BLENDFACT_ZERO <<
						   IAB_DST_FACTOR_SHIFT));
		OUT_BATCH(_3DSTATE_DFLT_DIFFUSE_CMD);
		OUT_BATCH(0);
		OUT_BATCH(_3DSTATE_DFLT_SPEC_CMD);
		OUT_BATCH(0);
		OUT_BATCH(_3DSTATE_DFLT_Z_CMD);
		OUT_BATCH(0);
		OUT_BATCH(_3DSTATE_COORD_SET_BINDINGS |
			  CSB_TCB(0, 0) |
			  CSB_TCB(1, 1) |
			  CSB_TCB(2, 2) |
			  CSB_TCB(3, 3) |
			  CSB_TCB(4, 4) |
			  CSB_TCB(5, 5) | CSB_TCB(6, 6) | CSB_TCB(7, 7));
		OUT_BATCH(_3DSTATE_RASTER_RULES_CMD |
			  ENABLE_POINT_RASTER_RULE |
			  OGL_POINT_RASTER_RULE |
			  ENABLE_LINE_STRIP_PROVOKE_VRTX |
			  ENABLE_TRI_FAN_PROVOKE_VRTX |
			  LINE_STRIP_PROVOKE_VRTX(1) |
			  TRI_FAN_PROVOKE_VRTX(2) | ENABLE_TEXKILL_3D_4D | TEXKILL_4D);
		OUT_BATCH(_3DSTATE_MODES_4_CMD |
			  ENABLE_LOGIC_OP_FUNC | LOGIC_OP_FUNC(LOGICOP_COPY) |
			  ENABLE_STENCIL_WRITE_MASK | STENCIL_WRITE_MASK(0xff) |
			  ENABLE_STENCIL_TEST_MASK | STENCIL_TEST_MASK(0xff));
		OUT_BATCH(_3DSTATE_LOAD_STATE_IMMEDIATE_1 | I1_LOAD_S(3) | I1_LOAD_S(4) | I1_LOAD_S(5) | 2);
		OUT_BATCH(0x00000000);	/* Disable texture coordinate wrap-shortest */
		OUT_BATCH((1 << S4_POINT_WIDTH_SHIFT) |
			  S4_LINE_WIDTH_ONE |
			  S4_CULLMODE_NONE |
			  S4_VFMT_XY);
		OUT_BATCH(0x00000000);	/* Stencil. */
		OUT_BATCH(_3DSTATE_SCISSOR_ENABLE_CMD | DISABLE_SCISSOR_RECT);
		OUT_BATCH(_3DSTATE_SCISSOR_RECT_0_CMD);
		OUT_BATCH(0);
		OUT_BATCH(0);
		OUT_BATCH(_3DSTATE_DEPTH_SUBRECT_DISABLE);
		OUT_BATCH(_3DSTATE_LOAD_INDIRECT | 0);	/* disable indirect state */
		OUT_BATCH(0);
		OUT_BATCH(_3DSTATE_STIPPLE);
		OUT_BATCH(0x00000000);
		OUT_BATCH(_3DSTATE_BACKFACE_STENCIL_OPS | BFO_ENABLE_STENCIL_TWO_SIDE | 0);
	}

	/* samler state */
	{
#define TEX_COUNT 1
		uint32_t tiling_bits = 0;
		if (src->tiling != I915_TILING_NONE)
			tiling_bits = MS3_TILED_SURFACE;
		if (src->tiling == I915_TILING_Y)
			tiling_bits |= MS3_TILE_WALK;

		OUT_BATCH(_3DSTATE_MAP_STATE | (3 * TEX_COUNT));
		OUT_BATCH((1 << TEX_COUNT) - 1);
		OUT_RELOC(src->bo, I915_GEM_DOMAIN_SAMPLER, 0, 0);
		OUT_BATCH(MAPSURF_32BIT | MT_32BIT_ARGB8888 |
			  tiling_bits |
			  (igt_buf_height(src) - 1) << MS3_HEIGHT_SHIFT |
			  (igt_buf_width(src) - 1) << MS3_WIDTH_SHIFT);
		OUT_BATCH((src->stride/4-1) << MS4_PITCH_SHIFT);

		OUT_BATCH(_3DSTATE_SAMPLER_STATE | (3 * TEX_COUNT));
		OUT_BATCH((1 << TEX_COUNT) - 1);
		OUT_BATCH(MIPFILTER_NONE << SS2_MIP_FILTER_SHIFT |
			  FILTER_NEAREST << SS2_MAG_FILTER_SHIFT |
			  FILTER_NEAREST << SS2_MIN_FILTER_SHIFT);
		OUT_BATCH(TEXCOORDMODE_WRAP << SS3_TCX_ADDR_MODE_SHIFT |
			  TEXCOORDMODE_WRAP << SS3_TCY_ADDR_MODE_SHIFT |
			  0 << SS3_TEXTUREMAP_INDEX_SHIFT);
		OUT_BATCH(0x00000000);
	}

	/* render target state */
	{
		uint32_t tiling_bits = 0;
		if (dst->tiling != I915_TILING_NONE)
			tiling_bits = BUF_3D_TILED_SURFACE;
		if (dst->tiling == I915_TILING_Y)
			tiling_bits |= BUF_3D_TILE_WALK_Y;

		OUT_BATCH(_3DSTATE_BUF_INFO_CMD);
		OUT_BATCH(BUF_3D_ID_COLOR_BACK | tiling_bits |
			  BUF_3D_PITCH(dst->stride));
		OUT_RELOC(dst->bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);

		OUT_BATCH(_3DSTATE_DST_BUF_VARS_CMD);
		OUT_BATCH(COLR_BUF_ARGB8888 |
			  DSTORG_HORT_BIAS(0x8) |
			  DSTORG_VERT_BIAS(0x8));

		/* draw rect is unconditional */
		OUT_BATCH(_3DSTATE_DRAW_RECT_CMD);
		OUT_BATCH(0x00000000);
		OUT_BATCH(0x00000000);	/* ymin, xmin */
		OUT_BATCH(DRAW_YMAX(igt_buf_height(dst) - 1) |
			  DRAW_XMAX(igt_buf_width(dst) - 1));
		/* yorig, xorig (relate to color buffer?) */
		OUT_BATCH(0x00000000);
	}

	/* texfmt */
	{
		OUT_BATCH(_3DSTATE_LOAD_STATE_IMMEDIATE_1 |
			  I1_LOAD_S(1) | I1_LOAD_S(2) | I1_LOAD_S(6) | 2);
		OUT_BATCH((4 << S1_VERTEX_WIDTH_SHIFT) |
			  (4 << S1_VERTEX_PITCH_SHIFT));
		OUT_BATCH(~S2_TEXCOORD_FMT(0, TEXCOORDFMT_NOT_PRESENT) | S2_TEXCOORD_FMT(0, TEXCOORDFMT_2D));
		OUT_BATCH(S6_CBUF_BLEND_ENABLE | S6_COLOR_WRITE_ENABLE |
			  BLENDFUNC_ADD << S6_CBUF_BLEND_FUNC_SHIFT |
			  BLENDFACT_ONE << S6_CBUF_SRC_BLEND_FACT_SHIFT |
			  BLENDFACT_ZERO << S6_CBUF_DST_BLEND_FACT_SHIFT);
	}

	/* frage shader */
	{
		OUT_BATCH(_3DSTATE_PIXEL_SHADER_PROGRAM | (1 + 3*3 - 2));
		/* decl FS_T0 */
		OUT_BATCH(D0_DCL |
			  REG_TYPE(FS_T0) << D0_TYPE_SHIFT |
			  REG_NR(FS_T0) << D0_NR_SHIFT |
			  ((REG_TYPE(FS_T0) != REG_TYPE_S) ? D0_CHANNEL_ALL : 0));
		OUT_BATCH(0);
		OUT_BATCH(0);
		/* decl FS_S0 */
		OUT_BATCH(D0_DCL |
			  (REG_TYPE(FS_S0) << D0_TYPE_SHIFT) |
			  (REG_NR(FS_S0) << D0_NR_SHIFT) |
			  ((REG_TYPE(FS_S0) != REG_TYPE_S) ? D0_CHANNEL_ALL : 0));
		OUT_BATCH(0);
		OUT_BATCH(0);
		/* texld(FS_OC, FS_S0, FS_T0 */
		OUT_BATCH(T0_TEXLD |
			  (REG_TYPE(FS_OC) << T0_DEST_TYPE_SHIFT) |
			  (REG_NR(FS_OC) << T0_DEST_NR_SHIFT) |
			  (REG_NR(FS_S0) << T0_SAMPLER_NR_SHIFT));
		OUT_BATCH((REG_TYPE(FS_T0) << T1_ADDRESS_REG_TYPE_SHIFT) |
			  (REG_NR(FS_T0) << T1_ADDRESS_REG_NR_SHIFT));
		OUT_BATCH(0);
	}

	OUT_BATCH(PRIM3D_RECTLIST | (3*4 - 1));
	emit_vertex(batch, dst_x + width);
	emit_vertex(batch, dst_y + height);
	emit_vertex(batch, src_x + width);
	emit_vertex(batch, src_y + height);

	emit_vertex(batch, dst_x);
	emit_vertex(batch, dst_y + height);
	emit_vertex(batch, src_x);
	emit_vertex(batch, src_y + height);

	emit_vertex(batch, dst_x);
	emit_vertex(batch, dst_y);
	emit_vertex(batch, src_x);
	emit_vertex(batch, src_y);

	intel_batchbuffer_flush(batch);
}

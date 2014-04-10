/*
 * Copyright © 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */


#include "intel_batchbuffer.h"
#include <lib/gen7_render.h>
#include <lib/intel_reg.h>
#include <stdio.h>

static const uint32_t ps_kernel[][4] = {
	{ 0x0080005a, 0x2e2077bd, 0x000000c0, 0x008d0040 },
	{ 0x0080005a, 0x2e6077bd, 0x000000d0, 0x008d0040 },
	{ 0x02800031, 0x21801fa9, 0x008d0e20, 0x08840001 },
	{ 0x00800001, 0x2e2003bd, 0x008d0180, 0x00000000 },
	{ 0x00800001, 0x2e6003bd, 0x008d01c0, 0x00000000 },
	{ 0x00800001, 0x2ea003bd, 0x008d0200, 0x00000000 },
	{ 0x00800001, 0x2ee003bd, 0x008d0240, 0x00000000 },
	{ 0x05800031, 0x20001fa8, 0x008d0e20, 0x90031000 },
};

static uint32_t
gen7_bind_buf_null(struct intel_batchbuffer *batch)
{
	uint32_t *ss;

	ss = intel_batch_state_alloc(batch, 8 * sizeof(*ss), 32);
	if (ss == NULL)
		return -1;

	ss[0] = 0;
	ss[1] = 0;
	ss[2] = 0;
	ss[3] = 0;
	ss[4] = 0;
	ss[5] = 0;
	ss[6] = 0;
	ss[7] = 0;

	return intel_batch_offset(batch, ss);
}

static void
gen7_emit_vertex_elements(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_VERTEX_ELEMENTS |
		  ((2 * (1 + 2)) + 1 - 2));

	OUT_BATCH(0 << GEN7_VE0_VERTEX_BUFFER_INDEX_SHIFT | GEN7_VE0_VALID |
		  GEN7_SURFACEFORMAT_R32G32B32A32_FLOAT <<
		  GEN7_VE0_FORMAT_SHIFT |
		  0 << GEN7_VE0_OFFSET_SHIFT);

	OUT_BATCH(GEN7_VFCOMPONENT_STORE_0 << GEN7_VE1_VFCOMPONENT_0_SHIFT |
		  GEN7_VFCOMPONENT_STORE_0 << GEN7_VE1_VFCOMPONENT_1_SHIFT |
		  GEN7_VFCOMPONENT_STORE_0 << GEN7_VE1_VFCOMPONENT_2_SHIFT |
		  GEN7_VFCOMPONENT_STORE_0 << GEN7_VE1_VFCOMPONENT_3_SHIFT);

	/* x,y */
	OUT_BATCH(0 << GEN7_VE0_VERTEX_BUFFER_INDEX_SHIFT | GEN7_VE0_VALID |
		  GEN7_SURFACEFORMAT_R16G16_SSCALED << GEN7_VE0_FORMAT_SHIFT |
		  0 << GEN7_VE0_OFFSET_SHIFT); /* offsets vb in bytes */
	OUT_BATCH(GEN7_VFCOMPONENT_STORE_SRC << GEN7_VE1_VFCOMPONENT_0_SHIFT |
		  GEN7_VFCOMPONENT_STORE_SRC << GEN7_VE1_VFCOMPONENT_1_SHIFT |
		  GEN7_VFCOMPONENT_STORE_0 << GEN7_VE1_VFCOMPONENT_2_SHIFT |
		  GEN7_VFCOMPONENT_STORE_1_FLT << GEN7_VE1_VFCOMPONENT_3_SHIFT);

	/* s,t */
	OUT_BATCH(0 << GEN7_VE0_VERTEX_BUFFER_INDEX_SHIFT | GEN7_VE0_VALID |
		  GEN7_SURFACEFORMAT_R16G16_SSCALED << GEN7_VE0_FORMAT_SHIFT |
		  4 << GEN7_VE0_OFFSET_SHIFT);  /* offset vb in bytes */
	OUT_BATCH(GEN7_VFCOMPONENT_STORE_SRC << GEN7_VE1_VFCOMPONENT_0_SHIFT |
		  GEN7_VFCOMPONENT_STORE_SRC << GEN7_VE1_VFCOMPONENT_1_SHIFT |
		  GEN7_VFCOMPONENT_STORE_0 << GEN7_VE1_VFCOMPONENT_2_SHIFT |
		  GEN7_VFCOMPONENT_STORE_1_FLT << GEN7_VE1_VFCOMPONENT_3_SHIFT);
}

static uint32_t
gen7_create_vertex_buffer(struct intel_batchbuffer *batch)
{
	uint16_t *v;

	v = intel_batch_state_alloc(batch, 12*sizeof(*v), 8);
	if (v == NULL)
		return -1;

	v[0] = 0;
	v[1] = 0;
	v[2] = 0;
	v[3] = 0;

	v[4] = 0;
	v[5] = 0;
	v[6] = 0;
	v[7] = 0;

	v[8] = 0;
	v[9] = 0;
	v[10] = 0;
	v[11] = 0;

	return intel_batch_offset(batch, v);
}

static void gen7_emit_vertex_buffer(struct intel_batchbuffer *batch)
{
	uint32_t offset;

	offset = gen7_create_vertex_buffer(batch);

	OUT_BATCH(GEN7_3DSTATE_VERTEX_BUFFERS | (5 - 2));
	OUT_BATCH(0 << GEN7_VB0_BUFFER_INDEX_SHIFT |
		  GEN7_VB0_VERTEXDATA |
		  GEN7_VB0_ADDRESS_MODIFY_ENABLE |
		  GEN7_VB0_NULL_VERTEX_BUFFER |
		  4*2 << GEN7_VB0_BUFFER_PITCH_SHIFT);

	OUT_RELOC(batch, I915_GEM_DOMAIN_VERTEX, 0, offset);
	OUT_BATCH(~0);
	OUT_BATCH(0);
}

static uint32_t
gen7_bind_surfaces(struct intel_batchbuffer *batch)
{
	uint32_t *binding_table;

	binding_table = intel_batch_state_alloc(batch, 8, 32);
	if (binding_table == NULL)
		return -1;

	binding_table[0] = gen7_bind_buf_null(batch);
	binding_table[1] = gen7_bind_buf_null(batch);

	return intel_batch_offset(batch, binding_table);
}

static void
gen7_emit_binding_table(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_BINDING_TABLE_POINTERS_PS | (2 - 2));
	OUT_BATCH(gen7_bind_surfaces(batch));
}

static void
gen7_emit_drawing_rectangle(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_DRAWING_RECTANGLE | (4 - 2));
	/* Purposedly set min > max for null rectangle */
	OUT_BATCH(0xffffffff);
	OUT_BATCH(0 | 0);
	OUT_BATCH(0);
}

static uint32_t
gen7_create_blend_state(struct intel_batchbuffer *batch)
{
	struct gen7_blend_state *blend;

	blend = intel_batch_state_alloc(batch, sizeof(*blend), 64);
	if (blend == NULL)
		return -1;

	blend->blend0.dest_blend_factor = GEN7_BLENDFACTOR_ZERO;
	blend->blend0.source_blend_factor = GEN7_BLENDFACTOR_ONE;
	blend->blend0.blend_func = GEN7_BLENDFUNCTION_ADD;
	blend->blend1.post_blend_clamp_enable = 1;
	blend->blend1.pre_blend_clamp_enable = 1;

	return intel_batch_offset(batch, blend);
}

static void
gen7_emit_state_base_address(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_STATE_BASE_ADDRESS | (10 - 2));
	OUT_BATCH(0);
	OUT_RELOC(batch, I915_GEM_DOMAIN_INSTRUCTION, 0, BASE_ADDRESS_MODIFY);
	OUT_RELOC(batch, I915_GEM_DOMAIN_INSTRUCTION, 0, BASE_ADDRESS_MODIFY);
	OUT_BATCH(0);
	OUT_RELOC(batch, I915_GEM_DOMAIN_INSTRUCTION, 0, BASE_ADDRESS_MODIFY);

	OUT_BATCH(0);
	OUT_BATCH(0 | BASE_ADDRESS_MODIFY);
	OUT_BATCH(0);
	OUT_BATCH(0 | BASE_ADDRESS_MODIFY);
}

static uint32_t
gen7_create_cc_viewport(struct intel_batchbuffer *batch)
{
	struct gen7_cc_viewport *vp;

	vp = intel_batch_state_alloc(batch, sizeof(*vp), 32);
	if (vp == NULL)
		return -1;

	vp->min_depth = -1.e35;
	vp->max_depth = 1.e35;

	return intel_batch_offset(batch, vp);
}

static void
gen7_emit_cc(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_BLEND_STATE_POINTERS | (2 - 2));
	OUT_BATCH(gen7_create_blend_state(batch));

	OUT_BATCH(GEN7_3DSTATE_VIEWPORT_STATE_POINTERS_CC | (2 - 2));
	OUT_BATCH(gen7_create_cc_viewport(batch));
}

static uint32_t
gen7_create_sampler(struct intel_batchbuffer *batch)
{
	struct gen7_sampler_state *ss;

	ss = intel_batch_state_alloc(batch, sizeof(*ss), 32);
	if (ss == NULL)
		return -1;

	ss->ss0.min_filter = GEN7_MAPFILTER_NEAREST;
	ss->ss0.mag_filter = GEN7_MAPFILTER_NEAREST;

	ss->ss3.r_wrap_mode = GEN7_TEXCOORDMODE_CLAMP;
	ss->ss3.s_wrap_mode = GEN7_TEXCOORDMODE_CLAMP;
	ss->ss3.t_wrap_mode = GEN7_TEXCOORDMODE_CLAMP;

	ss->ss3.non_normalized_coord = 1;

	return intel_batch_offset(batch, ss);
}

static void
gen7_emit_sampler(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_SAMPLER_STATE_POINTERS_PS | (2 - 2));
	OUT_BATCH(gen7_create_sampler(batch));
}

static void
gen7_emit_multisample(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_MULTISAMPLE | (4 - 2));
	OUT_BATCH(GEN7_3DSTATE_MULTISAMPLE_PIXEL_LOCATION_CENTER |
		  GEN7_3DSTATE_MULTISAMPLE_NUMSAMPLES_1); /* 1 sample/pixel */
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN7_3DSTATE_SAMPLE_MASK | (2 - 2));
	OUT_BATCH(1);
}

static void
gen7_emit_urb(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_PUSH_CONSTANT_ALLOC_PS | (2 - 2));
	OUT_BATCH(8); /* in 1KBs */

	/* num of VS entries must be divisible by 8 if size < 9 */
	OUT_BATCH(GEN7_3DSTATE_URB_VS | (2 - 2));
	OUT_BATCH((64 << GEN7_URB_ENTRY_NUMBER_SHIFT) |
		  (2 - 1) << GEN7_URB_ENTRY_SIZE_SHIFT |
		  (1 << GEN7_URB_STARTING_ADDRESS_SHIFT));

	OUT_BATCH(GEN7_3DSTATE_URB_HS | (2 - 2));
	OUT_BATCH((0 << GEN7_URB_ENTRY_SIZE_SHIFT) |
		  (2 << GEN7_URB_STARTING_ADDRESS_SHIFT));

	OUT_BATCH(GEN7_3DSTATE_URB_DS | (2 - 2));
	OUT_BATCH((0 << GEN7_URB_ENTRY_SIZE_SHIFT) |
		  (2 << GEN7_URB_STARTING_ADDRESS_SHIFT));

	OUT_BATCH(GEN7_3DSTATE_URB_GS | (2 - 2));
	OUT_BATCH((0 << GEN7_URB_ENTRY_SIZE_SHIFT) |
		  (1 << GEN7_URB_STARTING_ADDRESS_SHIFT));
}

static void
gen7_emit_vs(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_VS | (6 - 2));
	OUT_BATCH(0); /* no VS kernel */
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0); /* pass-through */
}

static void
gen7_emit_hs(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_HS | (7 - 2));
	OUT_BATCH(0); /* no HS kernel */
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0); /* pass-through */
}

static void
gen7_emit_te(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_TE | (4 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
}

static void
gen7_emit_ds(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_DS | (6 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
}

static void
gen7_emit_gs(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_GS | (7 - 2));
	OUT_BATCH(0); /* no GS kernel */
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0); /* pass-through  */
}

static void
gen7_emit_streamout(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_STREAMOUT | (3 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0);
}

static void
gen7_emit_sf(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_SF | (7 - 2));
	OUT_BATCH(0);
	OUT_BATCH(GEN7_3DSTATE_SF_CULL_NONE);
	OUT_BATCH(2 << GEN7_3DSTATE_SF_TRIFAN_PROVOKE_SHIFT);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
}

static void
gen7_emit_sbe(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_SBE | (14 - 2));
	OUT_BATCH(1 << GEN7_SBE_NUM_OUTPUTS_SHIFT |
		  1 << GEN7_SBE_URB_ENTRY_READ_LENGTH_SHIFT |
		  1 << GEN7_SBE_URB_ENTRY_READ_OFFSET_SHIFT);
	OUT_BATCH(0);
	OUT_BATCH(0); /* dw4 */
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0); /* dw8 */
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0); /* dw12 */
	OUT_BATCH(0);
	OUT_BATCH(0);
}

static void
gen7_emit_ps(struct intel_batchbuffer *batch)
{
	int threads;

#if 0 /* XXX: Do we need separate state for hsw or not */
	if (IS_HASWELL(batch->dev))
		threads = 40 << HSW_PS_MAX_THREADS_SHIFT |
			1 << HSW_PS_SAMPLE_MASK_SHIFT;
	else
#endif
		threads = 40 << IVB_PS_MAX_THREADS_SHIFT;

	OUT_BATCH(GEN7_3DSTATE_PS | (8 - 2));
	OUT_BATCH(intel_batch_state_copy(batch, ps_kernel,
					 sizeof(ps_kernel), 64));
	OUT_BATCH(1 << GEN7_PS_SAMPLER_COUNT_SHIFT |
		  2 << GEN7_PS_BINDING_TABLE_ENTRY_COUNT_SHIFT);
	OUT_BATCH(0); /* scratch address */
	OUT_BATCH(threads |
		  GEN7_PS_16_DISPATCH_ENABLE |
		  GEN7_PS_ATTRIBUTE_ENABLE);
	OUT_BATCH(6 << GEN7_PS_DISPATCH_START_GRF_SHIFT_0);
	OUT_BATCH(0);
	OUT_BATCH(0);
}

static void
gen7_emit_clip(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_CLIP | (4 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0); /* pass-through */
	OUT_BATCH(0);

	OUT_BATCH(GEN7_3DSTATE_VIEWPORT_STATE_POINTERS_SF_CL | (2 - 2));
	OUT_BATCH(0);
}

static void
gen7_emit_wm(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_WM | (3 - 2));
	OUT_BATCH(GEN7_WM_DISPATCH_ENABLE |
		  GEN7_WM_PERSPECTIVE_PIXEL_BARYCENTRIC);
	OUT_BATCH(0);
}

static void
gen7_emit_null_depth_buffer(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_DEPTH_BUFFER | (7 - 2));
	OUT_BATCH(GEN7_SURFACE_NULL << GEN7_3DSTATE_DEPTH_BUFFER_TYPE_SHIFT |
		  GEN7_DEPTHFORMAT_D32_FLOAT <<
		  GEN7_3DSTATE_DEPTH_BUFFER_FORMAT_SHIFT);
	OUT_BATCH(0); /* disable depth, stencil and hiz */
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN7_3DSTATE_CLEAR_PARAMS | (3 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0);
}

int gen7_setup_null_render_state(struct intel_batchbuffer *batch)
{
	int ret;

	OUT_BATCH(GEN7_PIPELINE_SELECT | PIPELINE_SELECT_3D);

	gen7_emit_state_base_address(batch);
	gen7_emit_multisample(batch);
	gen7_emit_urb(batch);
	gen7_emit_vs(batch);
	gen7_emit_hs(batch);
	gen7_emit_te(batch);
	gen7_emit_ds(batch);
	gen7_emit_gs(batch);
	gen7_emit_clip(batch);
	gen7_emit_sf(batch);
	gen7_emit_wm(batch);
	gen7_emit_streamout(batch);
	gen7_emit_null_depth_buffer(batch);

	gen7_emit_cc(batch);
	gen7_emit_sampler(batch);
	gen7_emit_sbe(batch);
	gen7_emit_ps(batch);
	gen7_emit_vertex_elements(batch);
	gen7_emit_vertex_buffer(batch);
	gen7_emit_binding_table(batch);
	gen7_emit_drawing_rectangle(batch);

	OUT_BATCH(GEN7_3DPRIMITIVE | (7 - 2));
	OUT_BATCH(GEN7_3DPRIMITIVE_VERTEX_SEQUENTIAL | _3DPRIM_RECTLIST);
	OUT_BATCH(3);
	OUT_BATCH(0);
	OUT_BATCH(1);   /* single instance */
	OUT_BATCH(0);   /* start instance location */
	OUT_BATCH(0);   /* index buffer offset, ignored */

	OUT_BATCH(MI_BATCH_BUFFER_END);

	ret = intel_batch_error(batch);
	if (ret == 0)
		ret = intel_batch_total_used(batch);

	return ret;
}

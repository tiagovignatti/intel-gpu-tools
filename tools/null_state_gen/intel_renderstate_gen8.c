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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Mika Kuoppala <mika.kuoppala@intel.com>
 */

#include "intel_renderstate.h"
#include "intel_batchbuffer.h"
#include <lib/gen8_render.h>
#include <lib/intel_reg.h>
#include <string.h>

static void gen8_emit_wm(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN6_3DSTATE_WM | (2 - 2));
	OUT_BATCH(GEN7_WM_LEGACY_DIAMOND_LINE_RASTERIZATION);
}

static void gen8_emit_ps(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_PS | (12 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0); /* kernel hi */
	OUT_BATCH(GEN7_PS_SPF_MODE);
	OUT_BATCH(0); /* scratch space stuff */
	OUT_BATCH(0); /* scratch hi */
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0); // kernel 1
	OUT_BATCH(0); /* kernel 1 hi */
	OUT_BATCH(0); // kernel 2
	OUT_BATCH(0); /* kernel 2 hi */
}

static void gen8_emit_sf(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN6_3DSTATE_SF | (4 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(1 << GEN6_3DSTATE_SF_TRIFAN_PROVOKE_SHIFT |
		  1 << GEN6_3DSTATE_SF_VERTEX_SUB_PIXEL_PRECISION_SHIFT |
		  GEN7_SF_POINT_WIDTH_FROM_SOURCE |
		  8);
}

static void gen8_emit_vs(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN6_3DSTATE_VS | (9 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(GEN7_VS_FLOATING_POINT_MODE_ALTERNATE);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
}

static void gen8_emit_hs(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN7_3DSTATE_HS | (9 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(1 << GEN7_SBE_URB_ENTRY_READ_LENGTH_SHIFT);
	OUT_BATCH(0);
}

static void gen8_emit_raster(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN8_3DSTATE_RASTER | (5 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0.0);
	OUT_BATCH(0.0);
	OUT_BATCH(0.0);
}

static void gen8_emit_urb(struct intel_batchbuffer *batch)
{
	const int vs_entries = 64;
	const int vs_size = 2;
	const int vs_start = 4;

	OUT_BATCH(GEN7_3DSTATE_URB_VS);
	OUT_BATCH(vs_entries | ((vs_size - 1) << 16) | (vs_start << 25));

	OUT_BATCH(GEN7_3DSTATE_URB_HS);
	OUT_BATCH(0x0f << 25);

	OUT_BATCH(GEN7_3DSTATE_URB_DS);
	OUT_BATCH(0x0f << 25);

	OUT_BATCH(GEN7_3DSTATE_URB_GS);
	OUT_BATCH(0x0f << 25);
}

static void gen8_emit_vf_topology(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN8_3DSTATE_VF_TOPOLOGY);
	OUT_BATCH(_3DPRIM_TRILIST);
}

static void gen8_emit_so_decl_list(struct intel_batchbuffer *batch)
{
	const int num_decls = 128;
	int i;

	OUT_BATCH(GEN8_3DSTATE_SO_DECL_LIST | ((2 * num_decls) + 1));
	OUT_BATCH(0);
	OUT_BATCH(num_decls);

	for (i = 0; i < num_decls; i++) {
		OUT_BATCH(0);
		OUT_BATCH(0);
	}
}

static void gen8_emit_so_buffer(struct intel_batchbuffer *batch, const int index)
{
	OUT_BATCH(GEN8_3DSTATE_SO_BUFFER | (8 - 2));
	OUT_BATCH(index << 29);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
}

static void gen8_emit_state_base_address(struct intel_batchbuffer *batch) {
	const unsigned offset = 0;
	OUT_BATCH(GEN6_STATE_BASE_ADDRESS | (16 - 2));

	/* general */
	OUT_RELOC(batch, 0, 0, offset | BASE_ADDRESS_MODIFY);
	OUT_BATCH(0);

	/* stateless data port */
	OUT_BATCH(0);

	/* surface state base addess */
	OUT_RELOC(batch, 0, 0, offset | BASE_ADDRESS_MODIFY);
	OUT_BATCH(0);

	/* dynamic state base address */
	OUT_RELOC(batch, 0, 0, offset | BASE_ADDRESS_MODIFY);
	OUT_BATCH(0);

	/* indirect */
	OUT_BATCH(BASE_ADDRESS_MODIFY);
	OUT_BATCH(0);

	/* instruction */
	OUT_RELOC(batch, 0, 0, offset | BASE_ADDRESS_MODIFY);
	OUT_BATCH(0);

	/* general state buffer size */
	OUT_BATCH(GEN8_STATE_SIZE_PAGES(1) | BUFFER_SIZE_MODIFY);
	/* dynamic state buffer size */
	OUT_BATCH(GEN8_STATE_SIZE_PAGES(1) | BUFFER_SIZE_MODIFY);
	/* indirect object buffer size */
	OUT_BATCH(0 | BUFFER_SIZE_MODIFY);
	/* intruction buffer size */
	OUT_BATCH(GEN8_STATE_SIZE_PAGES(1) | BUFFER_SIZE_MODIFY);
}

static void gen8_emit_chroma_key(struct intel_batchbuffer *batch, const int index)
{
	OUT_BATCH(GEN6_3DSTATE_CHROMA_KEY | (4 - 2));
	OUT_BATCH(index << 30);
	OUT_BATCH(0);
	OUT_BATCH(0);
}

static void gen8_emit_vertex_buffers(struct intel_batchbuffer *batch)
{
	const int buffers = 33;
	int i;

	OUT_BATCH(GEN6_3DSTATE_VERTEX_BUFFERS | ((4 * buffers) - 1));

	for (i = 0; i < buffers; i++) {
		OUT_BATCH(i << VB0_BUFFER_INDEX_SHIFT |
			  GEN7_VB0_BUFFER_ADDR_MOD_EN);
		OUT_BATCH(0); /* Addr */
		OUT_BATCH(0);
		OUT_BATCH(0);
	}
}

static void gen6_emit_vertex_elements(struct intel_batchbuffer *batch)
{
	const int elements = 34;
	int i;

	OUT_BATCH(GEN6_3DSTATE_VERTEX_ELEMENTS | ((2 * elements - 1)));

	for (i = 0; i < elements; i++) {
		if (i == 0) {
			OUT_BATCH(VE0_VALID | i);
			OUT_BATCH(
				GEN6_VFCOMPONENT_STORE_0 << VE1_VFCOMPONENT_0_SHIFT |
				GEN6_VFCOMPONENT_STORE_0 << VE1_VFCOMPONENT_1_SHIFT |
				GEN6_VFCOMPONENT_STORE_0 << VE1_VFCOMPONENT_2_SHIFT |
				GEN6_VFCOMPONENT_STORE_0 << VE1_VFCOMPONENT_3_SHIFT
				);
		} else {
			OUT_BATCH(0);
			OUT_BATCH(0);
		}
	}
}

static void gen8_emit_cc_state_pointers(struct intel_batchbuffer *batch)
{
	union {
		float fval;
		uint32_t uval;
	} u;

	unsigned offset;

	u.fval = 1.0f;

	offset = intel_batch_state_offset(batch, 64);
	OUT_STATE(0);
	OUT_STATE(0);      /* Alpha reference value */
	OUT_STATE(u.uval); /* Blend constant color RED */
	OUT_STATE(u.uval); /* Blend constant color BLUE */
	OUT_STATE(u.uval); /* Blend constant color GREEN */
	OUT_STATE(u.uval); /* Blend constant color ALPHA */

	OUT_BATCH(GEN6_3DSTATE_CC_STATE_POINTERS);
	OUT_BATCH_STATE_OFFSET(offset | 1);
}

static void gen8_emit_blend_state_pointers(struct intel_batchbuffer *batch)
{
	unsigned offset;
	int i;

	offset = intel_batch_state_offset(batch, 64);

	for (i = 0; i < 17; i++)
		OUT_STATE(0);

	OUT_BATCH(GEN7_3DSTATE_BLEND_STATE_POINTERS | (2 - 2));
	OUT_BATCH_STATE_OFFSET(offset | 1);
}

static void gen8_emit_ps_extra(struct intel_batchbuffer *batch)
{
        OUT_BATCH(GEN8_3DSTATE_PS_EXTRA | (2 - 2));
        OUT_BATCH(GEN8_PSX_PIXEL_SHADER_VALID |
		  GEN8_PSX_ATTRIBUTE_ENABLE);

}

static void gen8_emit_ps_blend(struct intel_batchbuffer *batch)
{
        OUT_BATCH(GEN8_3DSTATE_PS_BLEND | (2 - 2));
        OUT_BATCH(GEN8_PS_BLEND_HAS_WRITEABLE_RT);
}

static void gen8_emit_viewport_state_pointers_cc(struct intel_batchbuffer *batch)
{
	unsigned offset;

	offset = intel_batch_state_offset(batch, 32);

	OUT_STATE((uint32_t)0.0f); /* Minimum depth */
	OUT_STATE((uint32_t)0.0f); /* Maximum depth */

	OUT_BATCH(GEN7_3DSTATE_VIEWPORT_STATE_POINTERS_CC | (2 - 2));
	OUT_BATCH_STATE_OFFSET(offset);
}

static void gen8_emit_viewport_state_pointers_sf_clip(struct intel_batchbuffer *batch)
{
	unsigned offset;
	int i;

	offset = intel_batch_state_offset(batch, 64);

	for (i = 0; i < 16; i++)
		OUT_STATE(0);

	OUT_BATCH(GEN7_3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP | (2 - 2));
	OUT_BATCH_STATE_OFFSET(offset);
}

static void gen8_emit_primitive(struct intel_batchbuffer *batch)
{
        OUT_BATCH(GEN6_3DPRIMITIVE | (7-2));
        OUT_BATCH(4);   /* gen8+ ignore the topology type field */
        OUT_BATCH(1);   /* vertex count */
        OUT_BATCH(0);
        OUT_BATCH(1);   /* single instance */
        OUT_BATCH(0);   /* start instance location */
        OUT_BATCH(0);   /* index buffer offset, ignored */
}

void gen8_setup_null_render_state(struct intel_batchbuffer *batch)
{
#define GEN8_PIPE_CONTROL_GLOBAL_GTT   (1 << 24)

	OUT_BATCH(GEN6_PIPE_CONTROL | (6 - 2));
	OUT_BATCH(GEN8_PIPE_CONTROL_GLOBAL_GTT);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN6_PIPELINE_SELECT | PIPELINE_SELECT_3D);

	gen8_emit_wm(batch);
	gen8_emit_ps(batch);
	gen8_emit_sf(batch);

	OUT_CMD(GEN7_3DSTATE_SBE, 4);
	OUT_CMD(GEN8_3DSTATE_SBE_SWIZ, 11);

	gen8_emit_vs(batch);
	gen8_emit_hs(batch);

	OUT_CMD(GEN7_3DSTATE_GS, 10);
	OUT_CMD(GEN7_3DSTATE_STREAMOUT, 5);
	OUT_CMD(GEN7_3DSTATE_DS, 9);
	OUT_CMD(GEN6_3DSTATE_CLIP, 4);
	gen8_emit_raster(batch);
	OUT_CMD(GEN7_3DSTATE_TE, 4);
	OUT_CMD(GEN8_3DSTATE_VF, 2);
	OUT_CMD(GEN8_3DSTATE_WM_HZ_OP, 5);

	gen8_emit_urb(batch);

	OUT_CMD(GEN8_3DSTATE_BIND_TABLE_POOL_ALLOC, 4);
	OUT_CMD(GEN8_3DSTATE_GATHER_POOL_ALLOC, 4);
	OUT_CMD(GEN8_3DSTATE_DX9_CONSTANT_BUFFER_POOL_ALLOC, 4);
	OUT_CMD(GEN7_3DSTATE_PUSH_CONSTANT_ALLOC_VS, 2);
	OUT_CMD(GEN7_3DSTATE_PUSH_CONSTANT_ALLOC_HS, 2);
	OUT_CMD(GEN7_3DSTATE_PUSH_CONSTANT_ALLOC_DS, 2);
	OUT_CMD(GEN7_3DSTATE_PUSH_CONSTANT_ALLOC_GS, 2);
	OUT_CMD(GEN7_3DSTATE_PUSH_CONSTANT_ALLOC_PS, 2);
	OUT_CMD(GEN6_3DSTATE_CONSTANT_VS, 11);
	OUT_CMD(GEN7_3DSTATE_CONSTANT_HS, 11);
	OUT_CMD(GEN7_3DSTATE_CONSTANT_DS, 11);
	OUT_CMD(GEN7_3DSTATE_CONSTANT_GS, 11);
	OUT_CMD(GEN7_3DSTATE_CONSTANT_PS, 11);
	OUT_CMD(GEN8_3DSTATE_VF_INSTANCING, 3);
	OUT_CMD(GEN8_3DSTATE_VF_SGVS, 2);

	gen8_emit_vf_topology(batch);
	gen8_emit_so_decl_list(batch);

	gen8_emit_so_buffer(batch, 0);
	gen8_emit_so_buffer(batch, 1);
	gen8_emit_so_buffer(batch, 2);
	gen8_emit_so_buffer(batch, 3);

	gen8_emit_state_base_address(batch);

	OUT_CMD(GEN6_STATE_SIP, 3);
	OUT_CMD(GEN6_3DSTATE_DRAWING_RECTANGLE, 4);
	OUT_CMD(GEN7_3DSTATE_DEPTH_BUFFER, 8);

	gen8_emit_chroma_key(batch, 0);
	gen8_emit_chroma_key(batch, 1);
	gen8_emit_chroma_key(batch, 2);
	gen8_emit_chroma_key(batch, 3);

	OUT_CMD(GEN6_3DSTATE_LINE_STIPPLE, 3);
	OUT_CMD(GEN6_3DSTATE_AA_LINE_PARAMS, 3);
	OUT_CMD(GEN7_3DSTATE_STENCIL_BUFFER, 5);
	OUT_CMD(GEN7_3DSTATE_HIER_DEPTH_BUFFER, 5);
	OUT_CMD(GEN7_3DSTATE_CLEAR_PARAMS, 3);
	OUT_CMD(GEN6_3DSTATE_MONOFILTER_SIZE, 2);
	OUT_CMD(GEN8_3DSTATE_MULTISAMPLE, 2);
	OUT_CMD(GEN8_3DSTATE_POLY_STIPPLE_OFFSET, 2);
	OUT_CMD(GEN8_3DSTATE_POLY_STIPPLE_PATTERN, 33);
	OUT_CMD(GEN8_3DSTATE_SAMPLER_PALETTE_LOAD0, 16 + 1);
	OUT_CMD(GEN8_3DSTATE_SAMPLER_PALETTE_LOAD1, 16 + 1);
	OUT_CMD(GEN6_3DSTATE_INDEX_BUFFER, 5);

	gen8_emit_vertex_buffers(batch);
	gen6_emit_vertex_elements(batch);

	OUT_BATCH(GEN6_3DSTATE_VF_STATISTICS | 1); /* Enable */

	OUT_CMD(GEN7_3DSTATE_BINDING_TABLE_POINTERS_VS, 2);
	OUT_CMD(GEN7_3DSTATE_BINDING_TABLE_POINTERS_HS, 2);
	OUT_CMD(GEN7_3DSTATE_BINDING_TABLE_POINTERS_DS, 2);
	OUT_CMD(GEN7_3DSTATE_BINDING_TABLE_POINTERS_GS, 2);
	OUT_CMD(GEN7_3DSTATE_BINDING_TABLE_POINTERS_PS, 2);

	gen8_emit_cc_state_pointers(batch);
	gen8_emit_blend_state_pointers(batch);

	gen8_emit_ps_extra(batch);
	gen8_emit_ps_blend(batch);

	OUT_CMD(GEN7_3DSTATE_SAMPLER_STATE_POINTERS_VS, 2);
	OUT_CMD(GEN7_3DSTATE_SAMPLER_STATE_POINTERS_HS, 2);
	OUT_CMD(GEN7_3DSTATE_SAMPLER_STATE_POINTERS_DS, 2);
	OUT_CMD(GEN7_3DSTATE_SAMPLER_STATE_POINTERS_GS, 2);
	OUT_CMD(GEN7_3DSTATE_SAMPLER_STATE_POINTERS_PS, 2);

	OUT_CMD(GEN6_3DSTATE_SCISSOR_STATE_POINTERS, 2);

	gen8_emit_viewport_state_pointers_cc(batch);
	gen8_emit_viewport_state_pointers_sf_clip(batch);

	gen8_emit_primitive(batch);

	OUT_BATCH(MI_BATCH_BUFFER_END);
}

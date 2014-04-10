#include "intel_batchbuffer.h"
#include <lib/gen6_render.h>
#include <lib/intel_reg.h>
#include <string.h>

static const uint32_t ps_kernel_nomask_affine[][4] = {
	{ 0x0060005a, 0x204077be, 0x000000c0, 0x008d0040 },
	{ 0x0060005a, 0x206077be, 0x000000c0, 0x008d0080 },
	{ 0x0060005a, 0x208077be, 0x000000d0, 0x008d0040 },
	{ 0x0060005a, 0x20a077be, 0x000000d0, 0x008d0080 },
	{ 0x00000201, 0x20080061, 0x00000000, 0x00000000 },
	{ 0x00600001, 0x20200022, 0x008d0000, 0x00000000 },
	{ 0x02800031, 0x21c01cc9, 0x00000020, 0x0a8a0001 },
	{ 0x00600001, 0x204003be, 0x008d01c0, 0x00000000 },
	{ 0x00600001, 0x206003be, 0x008d01e0, 0x00000000 },
	{ 0x00600001, 0x208003be, 0x008d0200, 0x00000000 },
	{ 0x00600001, 0x20a003be, 0x008d0220, 0x00000000 },
	{ 0x00600001, 0x20c003be, 0x008d0240, 0x00000000 },
	{ 0x00600001, 0x20e003be, 0x008d0260, 0x00000000 },
	{ 0x00600001, 0x210003be, 0x008d0280, 0x00000000 },
	{ 0x00600001, 0x212003be, 0x008d02a0, 0x00000000 },
	{ 0x05800031, 0x24001cc8, 0x00000040, 0x90019000 },
	{ 0x0000007e, 0x00000000, 0x00000000, 0x00000000 },
	{ 0x0000007e, 0x00000000, 0x00000000, 0x00000000 },
	{ 0x0000007e, 0x00000000, 0x00000000, 0x00000000 },
	{ 0x0000007e, 0x00000000, 0x00000000, 0x00000000 },
	{ 0x0000007e, 0x00000000, 0x00000000, 0x00000000 },
	{ 0x0000007e, 0x00000000, 0x00000000, 0x00000000 },
	{ 0x0000007e, 0x00000000, 0x00000000, 0x00000000 },
	{ 0x0000007e, 0x00000000, 0x00000000, 0x00000000 },
};

static uint32_t
gen6_bind_buf_null(struct intel_batchbuffer *batch)
{
	struct gen6_surface_state *ss;
	int ret;

	ss = intel_batch_state_alloc(batch, sizeof(*ss), 32);
	if (ss == NULL)
		return -1;

	memset(ss, 0, sizeof(*ss));

	return intel_batch_offset(batch, ss);
}

static uint32_t
gen6_bind_surfaces(struct intel_batchbuffer *batch)
{
	uint32_t *binding_table;

	binding_table = intel_batch_state_alloc(batch, 32, 32);
	if (binding_table == NULL)
		return -1;

	binding_table[0] = gen6_bind_buf_null(batch);
	binding_table[1] = gen6_bind_buf_null(batch);

	return intel_batch_offset(batch, binding_table);
}

static void
gen6_emit_sip(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN6_STATE_SIP | 0);
	OUT_BATCH(0);
}

static void
gen6_emit_urb(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN6_3DSTATE_URB | (3 - 2));
	OUT_BATCH((1 - 1) << GEN6_3DSTATE_URB_VS_SIZE_SHIFT |
		  24 << GEN6_3DSTATE_URB_VS_ENTRIES_SHIFT); /* at least 24 on GEN6 */
	OUT_BATCH(0 << GEN6_3DSTATE_URB_GS_SIZE_SHIFT |
		  0 << GEN6_3DSTATE_URB_GS_ENTRIES_SHIFT); /* no GS thread */
}

static void
gen6_emit_state_base_address(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN6_STATE_BASE_ADDRESS | (10 - 2));
	OUT_BATCH(0); /* general */
	OUT_RELOC(batch,
		  I915_GEM_DOMAIN_INSTRUCTION, 0,
		  BASE_ADDRESS_MODIFY);
	OUT_RELOC(batch, /* instruction */
		  I915_GEM_DOMAIN_INSTRUCTION, 0,
		  BASE_ADDRESS_MODIFY);
	OUT_BATCH(0); /* indirect */
	OUT_RELOC(batch,
		  I915_GEM_DOMAIN_INSTRUCTION, 0,
		  BASE_ADDRESS_MODIFY);

	/* upper bounds, disable */
	OUT_BATCH(0);
	OUT_BATCH(BASE_ADDRESS_MODIFY);
	OUT_BATCH(0);
	OUT_BATCH(BASE_ADDRESS_MODIFY);
}

static void
gen6_emit_viewports(struct intel_batchbuffer *batch, uint32_t cc_vp)
{
	OUT_BATCH(GEN6_3DSTATE_VIEWPORT_STATE_POINTERS |
		  GEN6_3DSTATE_VIEWPORT_STATE_MODIFY_CC |
		  (4 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(cc_vp);
}

static void
gen6_emit_vs(struct intel_batchbuffer *batch)
{
	/* disable VS constant buffer */
	OUT_BATCH(GEN6_3DSTATE_CONSTANT_VS | (5 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN6_3DSTATE_VS | (6 - 2));
	OUT_BATCH(0); /* no VS kernel */
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0); /* pass-through */
}

static void
gen6_emit_gs(struct intel_batchbuffer *batch)
{
	/* disable GS constant buffer */
	OUT_BATCH(GEN6_3DSTATE_CONSTANT_GS | (5 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN6_3DSTATE_GS | (7 - 2));
	OUT_BATCH(0); /* no GS kernel */
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0); /* pass-through */
}

static void
gen6_emit_clip(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN6_3DSTATE_CLIP | (4 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0); /* pass-through */
	OUT_BATCH(0);
}

static void
gen6_emit_wm_constants(struct intel_batchbuffer *batch)
{
	/* disable WM constant buffer */
	OUT_BATCH(GEN6_3DSTATE_CONSTANT_PS | (5 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
}

static void
gen6_emit_null_depth_buffer(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN6_3DSTATE_DEPTH_BUFFER | (7 - 2));
	OUT_BATCH(GEN6_SURFACE_NULL << GEN6_3DSTATE_DEPTH_BUFFER_TYPE_SHIFT |
		  GEN6_DEPTHFORMAT_D32_FLOAT << GEN6_3DSTATE_DEPTH_BUFFER_FORMAT_SHIFT);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN6_3DSTATE_CLEAR_PARAMS | (2 - 2));
	OUT_BATCH(0);
}

static void
gen6_emit_invariant(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN6_PIPELINE_SELECT | PIPELINE_SELECT_3D);

	OUT_BATCH(GEN6_3DSTATE_MULTISAMPLE | (3 - 2));
	OUT_BATCH(GEN6_3DSTATE_MULTISAMPLE_PIXEL_LOCATION_CENTER |
		  GEN6_3DSTATE_MULTISAMPLE_NUMSAMPLES_1); /* 1 sample/pixel */
	OUT_BATCH(0);

	OUT_BATCH(GEN6_3DSTATE_SAMPLE_MASK | (2 - 2));
	OUT_BATCH(1);
}

static void
gen6_emit_cc(struct intel_batchbuffer *batch, uint32_t blend)
{
	OUT_BATCH(GEN6_3DSTATE_CC_STATE_POINTERS | (4 - 2));
	OUT_BATCH(blend | 1);
	OUT_BATCH(1024 | 1);
	OUT_BATCH(1024 | 1);
}

static void
gen6_emit_sampler(struct intel_batchbuffer *batch, uint32_t state)
{
	OUT_BATCH(GEN6_3DSTATE_SAMPLER_STATE_POINTERS |
		  GEN6_3DSTATE_SAMPLER_STATE_MODIFY_PS |
		  (4 - 2));
	OUT_BATCH(0); /* VS */
	OUT_BATCH(0); /* GS */
	OUT_BATCH(state);
}

static void
gen6_emit_sf(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN6_3DSTATE_SF | (20 - 2));
	OUT_BATCH(1 << GEN6_3DSTATE_SF_NUM_OUTPUTS_SHIFT |
		  1 << GEN6_3DSTATE_SF_URB_ENTRY_READ_LENGTH_SHIFT |
		  1 << GEN6_3DSTATE_SF_URB_ENTRY_READ_OFFSET_SHIFT);
	OUT_BATCH(0);
	OUT_BATCH(GEN6_3DSTATE_SF_CULL_NONE);
	OUT_BATCH(2 << GEN6_3DSTATE_SF_TRIFAN_PROVOKE_SHIFT); /* DW4 */
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0); /* DW9 */
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0); /* DW14 */
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0); /* DW19 */
}

static void
gen6_emit_wm(struct intel_batchbuffer *batch, int kernel)
{
	OUT_BATCH(GEN6_3DSTATE_WM | (9 - 2));
	OUT_BATCH(kernel);
	OUT_BATCH(1 << GEN6_3DSTATE_WM_SAMPLER_COUNT_SHIFT |
		  2 << GEN6_3DSTATE_WM_BINDING_TABLE_ENTRY_COUNT_SHIFT);
	OUT_BATCH(0);
	OUT_BATCH(6 << GEN6_3DSTATE_WM_DISPATCH_START_GRF_0_SHIFT); /* DW4 */
	OUT_BATCH((40 - 1) << GEN6_3DSTATE_WM_MAX_THREADS_SHIFT |
		  GEN6_3DSTATE_WM_DISPATCH_ENABLE |
		  GEN6_3DSTATE_WM_16_DISPATCH_ENABLE);
	OUT_BATCH(1 << GEN6_3DSTATE_WM_NUM_SF_OUTPUTS_SHIFT |
		  GEN6_3DSTATE_WM_PERSPECTIVE_PIXEL_BARYCENTRIC);
	OUT_BATCH(0);
	OUT_BATCH(0);
}

static void
gen6_emit_binding_table(struct intel_batchbuffer *batch, uint32_t wm_table)
{
	OUT_BATCH(GEN6_3DSTATE_BINDING_TABLE_POINTERS |
		  GEN6_3DSTATE_BINDING_TABLE_MODIFY_PS |
		  (4 - 2));
	OUT_BATCH(0);		/* vs */
	OUT_BATCH(0);		/* gs */
	OUT_BATCH(wm_table);
}

static void
gen6_emit_drawing_rectangle(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN6_3DSTATE_DRAWING_RECTANGLE | (4 - 2));
	OUT_BATCH(0xffffffff);
	OUT_BATCH(0 | 0);
	OUT_BATCH(0);
}

static void
gen6_emit_vertex_elements(struct intel_batchbuffer *batch)
{
	/* The VUE layout
	 *    dword 0-3: pad (0.0, 0.0, 0.0. 0.0)
	 *    dword 4-7: position (x, y, 1.0, 1.0),
	 *    dword 8-11: texture coordinate 0 (u0, v0, 0, 0)
	 *
	 * dword 4-11 are fetched from vertex buffer
	 */
	OUT_BATCH(GEN6_3DSTATE_VERTEX_ELEMENTS | (2 * 3 + 1 - 2));

	OUT_BATCH(0 << VE0_VERTEX_BUFFER_INDEX_SHIFT | VE0_VALID |
		  GEN6_SURFACEFORMAT_R32G32B32A32_FLOAT << VE0_FORMAT_SHIFT |
		  0 << VE0_OFFSET_SHIFT);
	OUT_BATCH(GEN6_VFCOMPONENT_STORE_0 << VE1_VFCOMPONENT_0_SHIFT |
		  GEN6_VFCOMPONENT_STORE_0 << VE1_VFCOMPONENT_1_SHIFT |
		  GEN6_VFCOMPONENT_STORE_0 << VE1_VFCOMPONENT_2_SHIFT |
		  GEN6_VFCOMPONENT_STORE_0 << VE1_VFCOMPONENT_3_SHIFT);

	/* x,y */
	OUT_BATCH(0 << VE0_VERTEX_BUFFER_INDEX_SHIFT | VE0_VALID |
		  GEN6_SURFACEFORMAT_R16G16_SSCALED << VE0_FORMAT_SHIFT |
		  0 << VE0_OFFSET_SHIFT); /* offsets vb in bytes */
	OUT_BATCH(GEN6_VFCOMPONENT_STORE_SRC << VE1_VFCOMPONENT_0_SHIFT |
		  GEN6_VFCOMPONENT_STORE_SRC << VE1_VFCOMPONENT_1_SHIFT |
		  GEN6_VFCOMPONENT_STORE_1_FLT << VE1_VFCOMPONENT_2_SHIFT |
		  GEN6_VFCOMPONENT_STORE_1_FLT << VE1_VFCOMPONENT_3_SHIFT);

	/* u0, v0 */
	OUT_BATCH(0 << VE0_VERTEX_BUFFER_INDEX_SHIFT | VE0_VALID |
		  GEN6_SURFACEFORMAT_R32G32_FLOAT << VE0_FORMAT_SHIFT |
		  4 << VE0_OFFSET_SHIFT);	/* offset vb in bytes */
	OUT_BATCH(GEN6_VFCOMPONENT_STORE_SRC << VE1_VFCOMPONENT_0_SHIFT |
		  GEN6_VFCOMPONENT_STORE_SRC << VE1_VFCOMPONENT_1_SHIFT |
		  GEN6_VFCOMPONENT_STORE_0 << VE1_VFCOMPONENT_2_SHIFT |
		  GEN6_VFCOMPONENT_STORE_0 << VE1_VFCOMPONENT_3_SHIFT);
}

static uint32_t
gen6_create_cc_viewport(struct intel_batchbuffer *batch)
{
	struct gen6_cc_viewport *vp;

	vp = intel_batch_state_alloc(batch, sizeof(*vp), 32);
	if (vp == NULL)
		return -1;

	vp->min_depth = -1.e35;
	vp->max_depth = 1.e35;

	return intel_batch_offset(batch, vp);
}

static uint32_t
gen6_create_cc_blend(struct intel_batchbuffer *batch)
{
	struct gen6_blend_state *blend;

	blend = intel_batch_state_alloc(batch, sizeof(*blend), 64);
	if (blend == NULL)
		return -1;

	blend->blend0.dest_blend_factor = GEN6_BLENDFACTOR_ZERO;
	blend->blend0.source_blend_factor = GEN6_BLENDFACTOR_ONE;
	blend->blend0.blend_func = GEN6_BLENDFUNCTION_ADD;
	blend->blend0.blend_enable = 1;

	blend->blend1.post_blend_clamp_enable = 1;
	blend->blend1.pre_blend_clamp_enable = 1;

	return intel_batch_offset(batch, blend);
}

static uint32_t
gen6_create_kernel(struct intel_batchbuffer *batch)
{
	return intel_batch_state_copy(batch, ps_kernel_nomask_affine,
				      sizeof(ps_kernel_nomask_affine),
				      64);
}

static uint32_t
gen6_create_sampler(struct intel_batchbuffer *batch,
		    sampler_filter_t filter,
		   sampler_extend_t extend)
{
	struct gen6_sampler_state *ss;

	ss = intel_batch_state_alloc(batch, sizeof(*ss), 32);
	if (ss == NULL)
		return -1;

	ss->ss0.lod_preclamp = 1;	/* GL mode */

	/* We use the legacy mode to get the semantics specified by
	 * the Render extension. */
	ss->ss0.border_color_mode = GEN6_BORDER_COLOR_MODE_LEGACY;

	switch (filter) {
	default:
	case SAMPLER_FILTER_NEAREST:
		ss->ss0.min_filter = GEN6_MAPFILTER_NEAREST;
		ss->ss0.mag_filter = GEN6_MAPFILTER_NEAREST;
		break;
	case SAMPLER_FILTER_BILINEAR:
		ss->ss0.min_filter = GEN6_MAPFILTER_LINEAR;
		ss->ss0.mag_filter = GEN6_MAPFILTER_LINEAR;
		break;
	}

	switch (extend) {
	default:
	case SAMPLER_EXTEND_NONE:
		ss->ss1.r_wrap_mode = GEN6_TEXCOORDMODE_CLAMP_BORDER;
		ss->ss1.s_wrap_mode = GEN6_TEXCOORDMODE_CLAMP_BORDER;
		ss->ss1.t_wrap_mode = GEN6_TEXCOORDMODE_CLAMP_BORDER;
		break;
	case SAMPLER_EXTEND_REPEAT:
		ss->ss1.r_wrap_mode = GEN6_TEXCOORDMODE_WRAP;
		ss->ss1.s_wrap_mode = GEN6_TEXCOORDMODE_WRAP;
		ss->ss1.t_wrap_mode = GEN6_TEXCOORDMODE_WRAP;
		break;
	case SAMPLER_EXTEND_PAD:
		ss->ss1.r_wrap_mode = GEN6_TEXCOORDMODE_CLAMP;
		ss->ss1.s_wrap_mode = GEN6_TEXCOORDMODE_CLAMP;
		ss->ss1.t_wrap_mode = GEN6_TEXCOORDMODE_CLAMP;
		break;
	case SAMPLER_EXTEND_REFLECT:
		ss->ss1.r_wrap_mode = GEN6_TEXCOORDMODE_MIRROR;
		ss->ss1.s_wrap_mode = GEN6_TEXCOORDMODE_MIRROR;
		ss->ss1.t_wrap_mode = GEN6_TEXCOORDMODE_MIRROR;
		break;
	}

	return intel_batch_offset(batch, ss);
}

static uint32_t
gen6_create_vertex_buffer(struct intel_batchbuffer *batch)
{
	uint16_t *v;

	v = intel_batch_state_alloc(batch, 2 * sizeof(uint16_t), 8);
	if (v == NULL)
		return -1;

	v[0] = 0;
	v[1] = 0;

	return intel_batch_offset(batch, v);
}

static void gen6_emit_vertex_buffer(struct intel_batchbuffer *batch)
{
	uint32_t offset;

	offset = gen6_create_vertex_buffer(batch);

	OUT_BATCH(GEN6_3DSTATE_VERTEX_BUFFERS | 3);
	OUT_BATCH(VB0_VERTEXDATA |
		  0 << VB0_BUFFER_INDEX_SHIFT |
		  VB0_NULL_VERTEX_BUFFER |
		  0 << VB0_BUFFER_PITCH_SHIFT);
	OUT_RELOC(batch, I915_GEM_DOMAIN_VERTEX, 0, offset);
	OUT_RELOC(batch, I915_GEM_DOMAIN_VERTEX, 0, offset);
	OUT_BATCH(0);
}

int gen6_setup_null_render_state(struct intel_batchbuffer *batch)
{
	uint32_t wm_state, wm_kernel, wm_table;
	uint32_t cc_vp, cc_blend, offset;
	uint32_t batch_end;
	int ret;

	wm_table  = gen6_bind_surfaces(batch);
	wm_kernel = gen6_create_kernel(batch);
	wm_state  = gen6_create_sampler(batch,
					SAMPLER_FILTER_NEAREST,
					SAMPLER_EXTEND_NONE);

	cc_vp = gen6_create_cc_viewport(batch);
	cc_blend = gen6_create_cc_blend(batch);

	gen6_emit_invariant(batch);
	gen6_emit_state_base_address(batch);

	gen6_emit_sip(batch);
	gen6_emit_urb(batch);

	gen6_emit_viewports(batch, cc_vp);
	gen6_emit_vs(batch);
	gen6_emit_gs(batch);
	gen6_emit_clip(batch);
	gen6_emit_wm_constants(batch);
	gen6_emit_null_depth_buffer(batch);

	gen6_emit_drawing_rectangle(batch);
	gen6_emit_cc(batch, cc_blend);
	gen6_emit_sampler(batch, wm_state);
	gen6_emit_sf(batch);
	gen6_emit_wm(batch, wm_kernel);
	gen6_emit_vertex_elements(batch);
	gen6_emit_binding_table(batch, wm_table);

	gen6_emit_vertex_buffer(batch);

	OUT_BATCH(MI_BATCH_BUFFER_END);

	ret = intel_batch_error(batch);
	if (ret == 0)
		ret = intel_batch_total_used(batch);

	return ret;
}

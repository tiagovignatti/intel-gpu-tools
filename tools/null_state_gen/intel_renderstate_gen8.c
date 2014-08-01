#include "intel_batchbuffer.h"
#include <lib/gen8_render.h>
#include <lib/intel_reg.h>
#include <string.h>

struct {
	uint32_t cc_state;
	uint32_t blend_state;
} cc;

struct {
	uint32_t cc_state;
	uint32_t sf_clip_state;
} viewport;

/* see shaders/ps/blit.g7a */
static const uint32_t ps_kernel[][4] = {
#if 1
   { 0x0060005a, 0x21403ae8, 0x3a0000c0, 0x008d0040 },
   { 0x0060005a, 0x21603ae8, 0x3a0000c0, 0x008d0080 },
   { 0x0060005a, 0x21803ae8, 0x3a0000d0, 0x008d0040 },
   { 0x0060005a, 0x21a03ae8, 0x3a0000d0, 0x008d0080 },
   { 0x02800031, 0x2e0022e8, 0x0e000140, 0x08840001 },
   { 0x05800031, 0x200022e0, 0x0e000e00, 0x90031000 },
#else
   /* Write all -1 */
   { 0x00600001, 0x2e000608, 0x00000000, 0x3f800000 },
   { 0x00600001, 0x2e200608, 0x00000000, 0x3f800000 },
   { 0x00600001, 0x2e400608, 0x00000000, 0x3f800000 },
   { 0x00600001, 0x2e600608, 0x00000000, 0x3f800000 },
   { 0x00600001, 0x2e800608, 0x00000000, 0x3f800000 },
   { 0x00600001, 0x2ea00608, 0x00000000, 0x3f800000 },
   { 0x00600001, 0x2ec00608, 0x00000000, 0x3f800000 },
   { 0x00600001, 0x2ee00608, 0x00000000, 0x3f800000 },
   { 0x05800031, 0x200022e0, 0x0e000e00, 0x90031000 },
#endif
};

static uint32_t
gen8_bind_buf_null(struct intel_batchbuffer *batch)
{
	struct gen8_surface_state ss;
	memset(&ss, 0, sizeof(ss));

	return OUT_STATE_STRUCT(ss, 64);
}

static uint32_t
gen8_bind_surfaces(struct intel_batchbuffer *batch)
{
	unsigned offset;

	offset = intel_batch_state_alloc(batch, 8, 32, "bind surfaces");

	bb_area_emit_offset(batch->state, offset, gen8_bind_buf_null(batch), STATE_OFFSET, "bind 1");
	bb_area_emit_offset(batch->state, offset + 4, gen8_bind_buf_null(batch), STATE_OFFSET, "bind 2");

	return offset;
}

/* Mostly copy+paste from gen6, except wrap modes moved */
static uint32_t
gen8_create_sampler(struct intel_batchbuffer *batch) {
	struct gen8_sampler_state ss;
	memset(&ss, 0, sizeof(ss));

	ss.ss0.min_filter = GEN6_MAPFILTER_NEAREST;
	ss.ss0.mag_filter = GEN6_MAPFILTER_NEAREST;
	ss.ss3.r_wrap_mode = GEN6_TEXCOORDMODE_CLAMP;
	ss.ss3.s_wrap_mode = GEN6_TEXCOORDMODE_CLAMP;
	ss.ss3.t_wrap_mode = GEN6_TEXCOORDMODE_CLAMP;

	/* I've experimented with non-normalized coordinates and using the LD
	 * sampler fetch, but couldn't make it work. */
	ss.ss3.non_normalized_coord = 0;

	return OUT_STATE_STRUCT(ss, 64);
}

static uint32_t
gen8_fill_ps(struct intel_batchbuffer *batch,
	     const uint32_t kernel[][4],
	     size_t size)
{
	return intel_batch_state_copy(batch, kernel, size, 64, "ps kernel");
}

/**
 * gen7_fill_vertex_buffer_data populate vertex buffer with data.
 *
 * The vertex buffer consists of 3 vertices to construct a RECTLIST. The 4th
 * vertex is implied (automatically derived by the HW). Each element has the
 * destination offset, and the normalized texture offset (src). The rectangle
 * itself will span the entire subsurface to be copied.
 *
 * see gen6_emit_vertex_elements
 */
static uint32_t
gen7_fill_vertex_buffer_data(struct intel_batchbuffer *batch)
{
	uint16_t *v;

	return intel_batch_state_alloc(batch, 2 * sizeof(*v), 8, "vertex buffer");
}

/**
 * gen6_emit_vertex_elements - The vertex elements describe the contents of the
 * vertex buffer. We pack the vertex buffer in a semi weird way, conforming to
 * what gen6_rendercopy did. The most straightforward would be to store
 * everything as floats.
 *
 * see gen7_fill_vertex_buffer_data() for where the corresponding elements are
 * packed.
 */
static void
gen6_emit_vertex_elements(struct intel_batchbuffer *batch) {
	/*
	 * The VUE layout
	 *    dword 0-3: pad (0, 0, 0. 0)
	 *    dword 4-7: position (x, y, 0, 1.0),
	 *    dword 8-11: texture coordinate 0 (u0, v0, 0, 1.0)
	 */
	OUT_BATCH(GEN6_3DSTATE_VERTEX_ELEMENTS | (3 * 2 + 1 - 2));

	/* Element state 0. These are 4 dwords of 0 required for the VUE format.
	 * We don't really know or care what they do.
	 */
	OUT_BATCH(0 << VE0_VERTEX_BUFFER_INDEX_SHIFT | VE0_VALID |
		  GEN6_SURFACEFORMAT_R32G32B32A32_FLOAT << VE0_FORMAT_SHIFT |
		  0 << VE0_OFFSET_SHIFT); /* we specify 0, but it's really does not exist */
	OUT_BATCH(GEN6_VFCOMPONENT_STORE_0 << VE1_VFCOMPONENT_0_SHIFT |
		  GEN6_VFCOMPONENT_STORE_0 << VE1_VFCOMPONENT_1_SHIFT |
		  GEN6_VFCOMPONENT_STORE_0 << VE1_VFCOMPONENT_2_SHIFT |
		  GEN6_VFCOMPONENT_STORE_0 << VE1_VFCOMPONENT_3_SHIFT);

	/* Element state 1 - Our "destination" vertices. These are passed down
	 * through the pipeline, and eventually make it to the pixel shader as
	 * the offsets in the destination surface. It's packed as the 16
	 * signed/scaled because of gen6 rendercopy. I see no particular reason
	 * for doing this though.
	 */
	OUT_BATCH(0 << VE0_VERTEX_BUFFER_INDEX_SHIFT | VE0_VALID |
		  GEN6_SURFACEFORMAT_R16G16_SSCALED << VE0_FORMAT_SHIFT |
		  0 << VE0_OFFSET_SHIFT); /* offsets vb in bytes */
	OUT_BATCH(GEN6_VFCOMPONENT_STORE_SRC << VE1_VFCOMPONENT_0_SHIFT |
		  GEN6_VFCOMPONENT_STORE_SRC << VE1_VFCOMPONENT_1_SHIFT |
		  GEN6_VFCOMPONENT_STORE_0 << VE1_VFCOMPONENT_2_SHIFT |
		  GEN6_VFCOMPONENT_STORE_1_FLT << VE1_VFCOMPONENT_3_SHIFT);

	/* Element state 2. Last but not least we store the U,V components as
	 * normalized floats. These will be used in the pixel shader to sample
	 * from the source buffer.
	 */
	OUT_BATCH(0 << VE0_VERTEX_BUFFER_INDEX_SHIFT | VE0_VALID |
		  GEN6_SURFACEFORMAT_R32G32_FLOAT << VE0_FORMAT_SHIFT |
		  4 << VE0_OFFSET_SHIFT);	/* offset vb in bytes */
	OUT_BATCH(GEN6_VFCOMPONENT_STORE_SRC << VE1_VFCOMPONENT_0_SHIFT |
		  GEN6_VFCOMPONENT_STORE_SRC << VE1_VFCOMPONENT_1_SHIFT |
		  GEN6_VFCOMPONENT_STORE_0 << VE1_VFCOMPONENT_2_SHIFT |
		  GEN6_VFCOMPONENT_STORE_1_FLT << VE1_VFCOMPONENT_3_SHIFT);
}

/**
 * gen7_emit_vertex_buffer emit the vertex buffers command
 *
 * @batch
 * @offset - bytw offset within the @batch where the vertex buffer starts.
 */
static void gen7_emit_vertex_buffer(struct intel_batchbuffer *batch,
				    uint32_t offset) {
	OUT_BATCH(GEN6_3DSTATE_VERTEX_BUFFERS | (1 + (4 * 1) - 2));
	OUT_BATCH(0 << VB0_BUFFER_INDEX_SHIFT | /* VB 0th index */
		  GEN7_VB0_BUFFER_ADDR_MOD_EN | /* Address Modify Enable */
		  VB0_NULL_VERTEX_BUFFER |
		  0 << VB0_BUFFER_PITCH_SHIFT);
	OUT_RELOC_STATE(batch, I915_GEM_DOMAIN_VERTEX, 0, offset);
	OUT_BATCH(0);
	OUT_BATCH(0);
}

static uint32_t
gen6_create_cc_state(struct intel_batchbuffer *batch)
{
	struct gen6_color_calc_state cc_state;
	memset(&cc_state, 0, sizeof(cc_state));

	return OUT_STATE_STRUCT(cc_state, 64);
}

static uint32_t
gen8_create_blend_state(struct intel_batchbuffer *batch)
{
	struct gen8_blend_state blend;
	int i;

	memset(&blend, 0, sizeof(blend));

	for (i = 0; i < 16; i++) {
		blend.bs[i].dest_blend_factor = GEN6_BLENDFACTOR_ZERO;
		blend.bs[i].source_blend_factor = GEN6_BLENDFACTOR_ONE;
		blend.bs[i].color_blend_func = GEN6_BLENDFUNCTION_ADD;
		blend.bs[i].pre_blend_color_clamp = 1;
		blend.bs[i].color_buffer_blend = 0;
	}

	return OUT_STATE_STRUCT(blend, 64);
}

static uint32_t
gen6_create_cc_viewport(struct intel_batchbuffer *batch)
{
	struct gen6_cc_viewport vp;

	memset(&vp, 0, sizeof(vp));

	/* XXX I don't understand this */
	vp.min_depth = -1.e35;
	vp.max_depth = 1.e35;

	return OUT_STATE_STRUCT(vp, 32);
}

static uint32_t
gen7_create_sf_clip_viewport(struct intel_batchbuffer *batch) {
	/* XXX these are likely not needed */
	struct gen7_sf_clip_viewport scv_state;

	memset(&scv_state, 0, sizeof(scv_state));

	scv_state.guardband.xmin = 0;
	scv_state.guardband.xmax = 1.0f;
	scv_state.guardband.ymin = 0;
	scv_state.guardband.ymax = 1.0f;

	return OUT_STATE_STRUCT(scv_state, 64);
}

static uint32_t
gen6_create_scissor_rect(struct intel_batchbuffer *batch)
{
	struct gen6_scissor_rect scissor;

	memset(&scissor, 0, sizeof(scissor));

	return OUT_STATE_STRUCT(scissor, 64);
}

static void
gen8_emit_sip(struct intel_batchbuffer *batch) {
	OUT_BATCH(GEN6_STATE_SIP | (3 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0);
}

static void
gen7_emit_push_constants(struct intel_batchbuffer *batch) {
	OUT_BATCH(GEN7_3DSTATE_PUSH_CONSTANT_ALLOC_VS);
	OUT_BATCH(0);
	OUT_BATCH(GEN7_3DSTATE_PUSH_CONSTANT_ALLOC_HS);
	OUT_BATCH(0);
	OUT_BATCH(GEN7_3DSTATE_PUSH_CONSTANT_ALLOC_DS);
	OUT_BATCH(0);
	OUT_BATCH(GEN7_3DSTATE_PUSH_CONSTANT_ALLOC_GS);
	OUT_BATCH(0);
	OUT_BATCH(GEN7_3DSTATE_PUSH_CONSTANT_ALLOC_PS);
	OUT_BATCH(0);
}

static void
gen8_emit_state_base_address(struct intel_batchbuffer *batch) {
	OUT_BATCH(GEN6_STATE_BASE_ADDRESS | (16 - 2));

	/* general */
	OUT_BATCH(0 | BASE_ADDRESS_MODIFY);
	OUT_BATCH(0);

	/* stateless data port */
	OUT_BATCH(0 | BASE_ADDRESS_MODIFY);

	/* surface */
	OUT_RELOC(batch, I915_GEM_DOMAIN_SAMPLER, 0, BASE_ADDRESS_MODIFY);
	OUT_BATCH(0);

	/* dynamic */
	OUT_RELOC(batch, I915_GEM_DOMAIN_RENDER | I915_GEM_DOMAIN_INSTRUCTION,
		  0, BASE_ADDRESS_MODIFY);
	OUT_BATCH(0);

	/* indirect */
	OUT_BATCH(0);
	OUT_BATCH(0);

	/* instruction */
	OUT_RELOC(batch, I915_GEM_DOMAIN_INSTRUCTION, 0, BASE_ADDRESS_MODIFY);
	OUT_BATCH(0);

	/* general state buffer size */
	OUT_BATCH(0xfffff000 | 1);
	/* dynamic state buffer size */
	OUT_BATCH(1 << 12 | 1);
	/* indirect object buffer size */
	OUT_BATCH(0xfffff000 | 1);
	/* intruction buffer size */
	OUT_BATCH(1 << 12 | 1);
}

static void
gen7_emit_urb(struct intel_batchbuffer *batch) {
	/* XXX: Min valid values from mesa */
	const int vs_entries = 64;
	const int vs_size = 2;
	const int vs_start = 4;

	OUT_BATCH(GEN7_3DSTATE_URB_VS);
	OUT_BATCH(vs_entries | ((vs_size - 1) << 16) | (vs_start << 25));
	OUT_BATCH(GEN7_3DSTATE_URB_GS);
	OUT_BATCH(vs_start << 25);
	OUT_BATCH(GEN7_3DSTATE_URB_HS);
	OUT_BATCH(vs_start << 25);
	OUT_BATCH(GEN7_3DSTATE_URB_DS);
	OUT_BATCH(vs_start << 25);
}

static void
gen8_emit_cc(struct intel_batchbuffer *batch) {
	OUT_BATCH(GEN7_3DSTATE_BLEND_STATE_POINTERS);
	OUT_BATCH_STATE_OFFSET(cc.blend_state | 1);

	OUT_BATCH(GEN6_3DSTATE_CC_STATE_POINTERS);
	OUT_BATCH_STATE_OFFSET(cc.cc_state | 1);
}

static void
gen8_emit_multisample(struct intel_batchbuffer *batch) {
	OUT_BATCH(GEN8_3DSTATE_MULTISAMPLE);
	OUT_BATCH(0);

	OUT_BATCH(GEN6_3DSTATE_SAMPLE_MASK);
	OUT_BATCH(1);
}

static void
gen8_emit_vs(struct intel_batchbuffer *batch) {
	OUT_BATCH(GEN7_3DSTATE_BINDING_TABLE_POINTERS_VS);
	OUT_BATCH(0);

	OUT_BATCH(GEN7_3DSTATE_SAMPLER_STATE_POINTERS_VS);
	OUT_BATCH(0);

	OUT_BATCH(GEN6_3DSTATE_CONSTANT_VS | (11 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN6_3DSTATE_VS | (9-2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
}

static void
gen8_emit_hs(struct intel_batchbuffer *batch) {
	OUT_BATCH(GEN7_3DSTATE_CONSTANT_HS | (11 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN7_3DSTATE_HS | (9-2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN7_3DSTATE_BINDING_TABLE_POINTERS_HS);
	OUT_BATCH(0);

	OUT_BATCH(GEN7_3DSTATE_SAMPLER_STATE_POINTERS_HS);
	OUT_BATCH(0);
}

static void
gen8_emit_gs(struct intel_batchbuffer *batch) {
	OUT_BATCH(GEN7_3DSTATE_CONSTANT_GS | (11 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN7_3DSTATE_GS | (10-2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN7_3DSTATE_BINDING_TABLE_POINTERS_GS);
	OUT_BATCH(0);

	OUT_BATCH(GEN7_3DSTATE_SAMPLER_STATE_POINTERS_GS);
	OUT_BATCH(0);
}

static void
gen8_emit_ds(struct intel_batchbuffer *batch) {
	OUT_BATCH(GEN7_3DSTATE_CONSTANT_DS | (11 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN7_3DSTATE_DS | (9-2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN7_3DSTATE_BINDING_TABLE_POINTERS_DS);
	OUT_BATCH(0);

	OUT_BATCH(GEN7_3DSTATE_SAMPLER_STATE_POINTERS_DS);
	OUT_BATCH(0);
}

static void
gen8_emit_wm_hz_op(struct intel_batchbuffer *batch) {
	OUT_BATCH(GEN8_3DSTATE_WM_HZ_OP | (5-2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
}

static void
gen8_emit_null_state(struct intel_batchbuffer *batch) {
	gen8_emit_wm_hz_op(batch);
	gen8_emit_hs(batch);
	OUT_BATCH(GEN7_3DSTATE_TE | (4-2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	gen8_emit_gs(batch);
	gen8_emit_ds(batch);
	gen8_emit_vs(batch);
}

static void
gen7_emit_clip(struct intel_batchbuffer *batch) {
	OUT_BATCH(GEN6_3DSTATE_CLIP | (4 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0); /*  pass-through */
	OUT_BATCH(0);
}

static void
gen8_emit_sf(struct intel_batchbuffer *batch)
{
	int i;

	OUT_BATCH(GEN7_3DSTATE_SBE | (4 - 2));
	OUT_BATCH(1 << GEN7_SBE_NUM_OUTPUTS_SHIFT |
		  GEN8_SBE_FORCE_URB_ENTRY_READ_LENGTH |
		  GEN8_SBE_FORCE_URB_ENTRY_READ_OFFSET |
		  1 << GEN7_SBE_URB_ENTRY_READ_LENGTH_SHIFT |
		  1 << GEN8_SBE_URB_ENTRY_READ_OFFSET_SHIFT);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN8_3DSTATE_SBE_SWIZ | (11 - 2));
	for (i = 0; i < 8; i++)
		OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN8_3DSTATE_RASTER | (5 - 2));
	OUT_BATCH(GEN8_RASTER_FRONT_WINDING_CCW | GEN8_RASTER_CULL_NONE);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN6_3DSTATE_SF | (4 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
}

static void
gen8_emit_ps(struct intel_batchbuffer *batch, uint32_t kernel) {
	const int max_threads = 63;

	OUT_BATCH(GEN6_3DSTATE_WM | (2 - 2));
	OUT_BATCH(/* XXX: I don't understand the BARYCENTRIC stuff, but it
		   * appears we need it to put our setup data in the place we
		   * expect (g6, see below) */
		  GEN7_3DSTATE_PS_PERSPECTIVE_PIXEL_BARYCENTRIC);

	OUT_BATCH(GEN6_3DSTATE_CONSTANT_PS | (11-2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN7_3DSTATE_PS | (12-2));
	OUT_BATCH_STATE_OFFSET(kernel);
	OUT_BATCH(0); /* kernel hi */
	OUT_BATCH(1 << GEN6_3DSTATE_WM_SAMPLER_COUNT_SHIFT |
		  2 << GEN6_3DSTATE_WM_BINDING_TABLE_ENTRY_COUNT_SHIFT);
	OUT_BATCH(0); /* scratch space stuff */
	OUT_BATCH(0); /* scratch hi */
	OUT_BATCH((max_threads - 1) << GEN8_3DSTATE_PS_MAX_THREADS_SHIFT |
		  GEN6_3DSTATE_WM_16_DISPATCH_ENABLE);
	OUT_BATCH(6 << GEN6_3DSTATE_WM_DISPATCH_START_GRF_0_SHIFT);
	OUT_BATCH(0); // kernel 1
	OUT_BATCH(0); /* kernel 1 hi */
	OUT_BATCH(0); // kernel 2
	OUT_BATCH(0); /* kernel 2 hi */

	OUT_BATCH(GEN8_3DSTATE_PS_BLEND | (2 - 2));
	OUT_BATCH(GEN8_PS_BLEND_HAS_WRITEABLE_RT);

	OUT_BATCH(GEN8_3DSTATE_PS_EXTRA | (2 - 2));
	OUT_BATCH(GEN8_PSX_PIXEL_SHADER_VALID | GEN8_PSX_ATTRIBUTE_ENABLE);
}

static void
gen8_emit_depth(struct intel_batchbuffer *batch) {
	OUT_BATCH(GEN7_3DSTATE_DEPTH_BUFFER | (8-2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN7_3DSTATE_HIER_DEPTH_BUFFER | (5 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN7_3DSTATE_STENCIL_BUFFER | (5 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
}

static void
gen7_emit_clear(struct intel_batchbuffer *batch) {
	OUT_BATCH(GEN7_3DSTATE_CLEAR_PARAMS | (3-2));
	OUT_BATCH(0);
	OUT_BATCH(1); // clear valid
}

static void
gen6_emit_drawing_rectangle(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN6_3DSTATE_DRAWING_RECTANGLE | (4 - 2));
	OUT_BATCH(0xffffffff);
	OUT_BATCH(0 | 0);
	OUT_BATCH(0);
}

static void gen8_emit_vf_topology(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN8_3DSTATE_VF_TOPOLOGY);
	OUT_BATCH(_3DPRIM_RECTLIST);
}

/* Vertex elements MUST be defined before this according to spec */
static void gen8_emit_primitive(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN8_3DSTATE_VF_INSTANCING | (3 - 2));
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN6_3DPRIMITIVE | (7-2));
	OUT_BATCH(0);	/* gen8+ ignore the topology type field */
	OUT_BATCH(3);	/* vertex count */
	OUT_BATCH(0);	/*  We're specifying this instead with offset in GEN6_3DSTATE_VERTEX_BUFFERS */
	OUT_BATCH(1);	/* single instance */
	OUT_BATCH(0);	/* start instance location */
	OUT_BATCH(0);	/* index buffer offset, ignored */
}

void gen8_setup_null_render_state(struct intel_batchbuffer *batch)
{
	uint32_t ps_sampler_state, ps_kernel_off, ps_binding_table;
	uint32_t scissor_state;
	uint32_t vertex_buffer;
	uint32_t batch_end;
	int ret;

	ps_binding_table  = gen8_bind_surfaces(batch);
	ps_sampler_state  = gen8_create_sampler(batch);
	ps_kernel_off = gen8_fill_ps(batch, ps_kernel, sizeof(ps_kernel));
	vertex_buffer = gen7_fill_vertex_buffer_data(batch);
	cc.cc_state = gen6_create_cc_state(batch);
	cc.blend_state = gen8_create_blend_state(batch);
	viewport.cc_state = gen6_create_cc_viewport(batch);
	viewport.sf_clip_state = gen7_create_sf_clip_viewport(batch);
	scissor_state = gen6_create_scissor_rect(batch);
	/* TODO: theree is other state which isn't setup */

	/* Start emitting the commands. The order roughly follows the mesa blorp
	 * order */
	OUT_BATCH(GEN6_PIPELINE_SELECT | PIPELINE_SELECT_3D);

	gen8_emit_sip(batch);

	gen7_emit_push_constants(batch);

	gen8_emit_state_base_address(batch);

	OUT_BATCH(GEN7_3DSTATE_VIEWPORT_STATE_POINTERS_CC);
	OUT_BATCH_STATE_OFFSET(viewport.cc_state);
	OUT_BATCH(GEN7_3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP);
	OUT_BATCH_STATE_OFFSET(viewport.sf_clip_state);

	gen7_emit_urb(batch);

	gen8_emit_cc(batch);

	gen8_emit_multisample(batch);

	gen8_emit_null_state(batch);

	OUT_BATCH(GEN7_3DSTATE_STREAMOUT | (5-2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	gen7_emit_clip(batch);

	gen8_emit_sf(batch);

	OUT_BATCH(GEN7_3DSTATE_BINDING_TABLE_POINTERS_PS);
	OUT_BATCH_STATE_OFFSET(ps_binding_table);

	OUT_BATCH(GEN7_3DSTATE_SAMPLER_STATE_POINTERS_PS);
	OUT_BATCH_STATE_OFFSET(ps_sampler_state);

	gen8_emit_ps(batch, ps_kernel_off);

	OUT_BATCH(GEN6_3DSTATE_SCISSOR_STATE_POINTERS);
	OUT_BATCH_STATE_OFFSET(scissor_state);

	gen8_emit_depth(batch);

	gen7_emit_clear(batch);

	gen6_emit_drawing_rectangle(batch);

	gen7_emit_vertex_buffer(batch, vertex_buffer);
	gen6_emit_vertex_elements(batch);

	gen8_emit_vf_topology(batch);
	gen8_emit_primitive(batch);

	OUT_BATCH(MI_BATCH_BUFFER_END);
}

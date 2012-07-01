#include "rendercopy.h"
#include "gen7_render.h"

#include <assert.h>

#define ALIGN(x, y) (((x) + (y)-1) & ~((y)-1))
#define VERTEX_SIZE (3*4)

#if DEBUG_RENDERCPY
static void dump_batch(struct intel_batchbuffer *batch)
#else
#define dump_batch(x) do { } while(0)
#endif

struct {
	uint32_t cc_state;
	uint32_t blend_state;
	uint32_t ds_state;
} cc;

struct {
	uint32_t cc_state;
	uint32_t sf_clip_state;
} viewport;

/* see shaders/ps/blit.g7a */
static const uint32_t ps_kernel[][4] = {
#if 1
   { 0x0060005a, 0x214077bd, 0x000000c0, 0x008d0040 },
   { 0x0060005a, 0x216077bd, 0x000000c0, 0x008d0080 },
   { 0x0060005a, 0x218077bd, 0x000000d0, 0x008d0040 },
   { 0x0060005a, 0x21a077bd, 0x000000d0, 0x008d0080 },
   { 0x02800031, 0x2e001e3d, 0x00000140, 0x08840001 },
   { 0x05800031, 0x20001e3c, 0x00000e00, 0x90031000 },

#else
   /* Write all -1 */
   { 0x00600001, 0x2e000061, 0x00000000, 0x3f800000 },
   { 0x00600001, 0x2e200061, 0x00000000, 0x3f800000 },
   { 0x00600001, 0x2e400061, 0x00000000, 0x3f800000 },
   { 0x00600001, 0x2e600061, 0x00000000, 0x3f800000 },
   { 0x00600001, 0x2e800061, 0x00000000, 0x3f800000 },
   { 0x00600001, 0x2ea00061, 0x00000000, 0x3f800000 },
   { 0x00600001, 0x2ec00061, 0x00000000, 0x3f800000 },
   { 0x00600001, 0x2ee00061, 0x00000000, 0x3f800000 },
   { 0x05800031, 0x20001e3c, 0x00000e00, 0x90031000 },
#endif
};

static uint32_t
batch_used(struct intel_batchbuffer *batch)
{
	return batch->ptr - batch->buffer;
}

static uint32_t
batch_align(struct intel_batchbuffer *batch, uint32_t align)
{
	uint32_t offset = batch_used(batch);
	offset = ALIGN(offset, align);
	batch->ptr = batch->buffer + offset;
	return offset;
}

static void *
batch_alloc(struct intel_batchbuffer *batch, uint32_t size, uint32_t align)
{
	uint32_t offset = batch_align(batch, align);
	batch->ptr += size;
	return memset(batch->buffer + offset, 0, size);
}

static uint32_t
batch_offset(struct intel_batchbuffer *batch, void *ptr)
{
	return (uint8_t *)ptr - batch->buffer;
}

static uint32_t
batch_copy(struct intel_batchbuffer *batch, const void *ptr, uint32_t size, uint32_t align)
{
	return batch_offset(batch, memcpy(batch_alloc(batch, size, align), ptr, size));
}

static void
gen6_render_flush(struct intel_batchbuffer *batch, uint32_t batch_end)
{
	int ret;

	ret = drm_intel_bo_subdata(batch->bo, 0, 4096, batch->buffer);
	if (ret == 0)
		ret = drm_intel_bo_mrb_exec(batch->bo, batch_end,
					    NULL, 0, 0, 0);
	assert(ret == 0);
}

/* Mostly copy+paste from gen6, except height, width, pitch moved */
static uint32_t
gen7_bind_buf(struct intel_batchbuffer *batch, struct scratch_buf *buf,
	      uint32_t format, int is_dst) {
	struct gen7_surface_state *ss;
	uint32_t write_domain, read_domain;
	int ret;

	if (is_dst) {
		write_domain = read_domain = I915_GEM_DOMAIN_RENDER;
	} else {
		write_domain = 0;
		read_domain = I915_GEM_DOMAIN_SAMPLER;
	}

	ss = batch_alloc(batch, sizeof(*ss), 32);
	ss->ss0.surface_type = GEN6_SURFACE_2D;
	ss->ss0.surface_format = format;
	ss->ss0.render_cache_read_write = 1; /* GEN7+ */
	ss->ss0.tiled_surface = buf->tiling != I915_TILING_NONE;
	ss->ss0.tile_walk     = buf->tiling == I915_TILING_Y;

	ss->ss1.base_addr = buf->bo->offset;

	ret = drm_intel_bo_emit_reloc(batch->bo,
				      batch_offset(batch, ss) + 4,
				      buf->bo, 0,
				      read_domain, write_domain);
	assert(ret == 0);

	ss->ss2.height = buf_height(buf) - 1;
	ss->ss2.width  = buf_width(buf) - 1;
	ss->ss3.pitch  = buf->stride - 1;

	if (IS_HASWELL(batch->devid)) {
		ss->ss7.shader_chanel_select_a = 4;
		ss->ss7.shader_chanel_select_g = 5;
		ss->ss7.shader_chanel_select_b = 6;
		ss->ss7.shader_chanel_select_a = 7;
	}

	return batch_offset(batch, ss);
}

static uint32_t
gen7_bind_surfaces(struct intel_batchbuffer *batch,
		   struct scratch_buf *src,
		   struct scratch_buf *dst) {
	uint32_t *binding_table;

	binding_table = batch_alloc(batch, 8, 32);

	binding_table[0] =
		gen7_bind_buf(batch, dst, GEN6_SURFACEFORMAT_B8G8R8A8_UNORM, 1);
	binding_table[1] =
		gen7_bind_buf(batch, src, GEN6_SURFACEFORMAT_B8G8R8A8_UNORM, 0);

	return batch_offset(batch, binding_table);
}

/* Mostly copy+paste from gen6, except wrap modes moved */
static uint32_t
gen7_create_sampler(struct intel_batchbuffer *batch) {
	struct gen7_sampler_state *ss;

	ss = batch_alloc(batch, sizeof(*ss), 32);

	ss->ss0.min_filter = GEN6_MAPFILTER_NEAREST;
	ss->ss0.mag_filter = GEN6_MAPFILTER_NEAREST;
	ss->ss3.r_wrap_mode = GEN6_TEXCOORDMODE_CLAMP;
	ss->ss3.s_wrap_mode = GEN6_TEXCOORDMODE_CLAMP;
	ss->ss3.t_wrap_mode = GEN6_TEXCOORDMODE_CLAMP;

	/* I've experimented with non-normalized coordinates and using the LD
	 * sampler fetch, but couldn't make it work. */
	ss->ss3.non_normalized_coord = 0;

	return batch_offset(batch, ss);
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
gen7_fill_vertex_buffer_data(struct intel_batchbuffer *batch,
			     struct scratch_buf *src,
			     uint32_t src_x, uint32_t src_y,
			     uint32_t dst_x, uint32_t dst_y,
			     uint32_t width, uint32_t height) {
	void *ret;

	ret = batch->ptr;

	emit_vertex_2s(batch, dst_x + width, dst_y + height);
	emit_vertex_normalized(batch, src_x + width, buf_width(src));
	emit_vertex_normalized(batch, src_y + height, buf_height(src));

	emit_vertex_2s(batch, dst_x, dst_y + height);
	emit_vertex_normalized(batch, src_x, buf_width(src));
	emit_vertex_normalized(batch, src_y + height, buf_height(src));

	emit_vertex_2s(batch, dst_x, dst_y);
	emit_vertex_normalized(batch, src_x, buf_width(src));
	emit_vertex_normalized(batch, src_y, buf_height(src));

	return batch_offset(batch, ret);
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
	OUT_BATCH(GEN6_3DSTATE_VERTEX_BUFFERS | (4 * 1 - 1));
	OUT_BATCH(0 << VB0_BUFFER_INDEX_SHIFT | /* VB 0th index */
		  VB0_VERTEXDATA |
		  GEN7_VB0_BUFFER_ADDR_MOD_EN | /* Address Modify Enable */
		  VERTEX_SIZE << VB0_BUFFER_PITCH_SHIFT);
	OUT_RELOC(batch->bo, I915_GEM_DOMAIN_VERTEX, 0, offset);
	OUT_RELOC(batch->bo, I915_GEM_DOMAIN_VERTEX, 0, offset + (VERTEX_SIZE * 3) - 1);
	OUT_BATCH(0);
}

static uint32_t
gen6_create_cc_state(struct intel_batchbuffer *batch)
{
	struct gen6_color_calc_state *cc_state;
	cc_state = batch_alloc(batch, sizeof(*cc_state), 64);
	return batch_offset(batch, cc_state);
}

static uint32_t
gen6_create_depth_stencil_state(struct intel_batchbuffer *batch)
{
	struct gen6_depth_stencil_state *depth;
	depth = batch_alloc(batch, sizeof(*depth), 64);
	depth->ds0.stencil_enable = 0;
	return batch_offset(batch, depth);
}

static uint32_t
gen6_create_blend_state(struct intel_batchbuffer *batch)
{
	struct gen6_blend_state *blend;
	blend = batch_alloc(batch, sizeof(*blend), 64);
	blend->blend0.blend_enable = 0;
	blend->blend1.pre_blend_clamp_enable = 1;
	return batch_offset(batch, blend);
}

static uint32_t
gen6_create_cc_viewport(struct intel_batchbuffer *batch)
{
	struct gen6_cc_viewport *vp;

	vp = batch_alloc(batch, sizeof(*vp), 32);
	/* XXX I don't understand this */
	vp->min_depth = -1.e35;
	vp->max_depth = 1.e35;
	return batch_offset(batch, vp);
}

static uint32_t
gen7_create_sf_clip_viewport(struct intel_batchbuffer *batch) {
	/* XXX these are likely not needed */
	struct gen7_sf_clip_viewport *scv_state;
	scv_state = batch_alloc(batch, sizeof(*scv_state), 64);
	scv_state->guardband.xmin = 0;
	scv_state->guardband.xmax = 1.0f;
	scv_state->guardband.ymin = 0;
	scv_state->guardband.ymax = 1.0f;
	return batch_offset(batch, scv_state);
}

static uint32_t
gen6_create_scissor_rect(struct intel_batchbuffer *batch)
{
	struct gen6_scissor_rect *scissor;
	scissor = batch_alloc(batch, sizeof(*scissor), 64);
	return batch_offset(batch, scissor);
}





static void
gen6_emit_sip(struct intel_batchbuffer *batch) {
	OUT_BATCH(GEN6_STATE_SIP | 0);
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
gen7_emit_state_base_address(struct intel_batchbuffer *batch) {
	OUT_BATCH(GEN6_STATE_BASE_ADDRESS | (10 - 2));
	/* general (stateless) */
	/* surface */
	/* instruction */
	/* indirect */
	/* dynamic */
	OUT_BATCH(0 | BASE_ADDRESS_MODIFY);
	OUT_RELOC(batch->bo, I915_GEM_DOMAIN_SAMPLER, 0, BASE_ADDRESS_MODIFY);
	OUT_RELOC(batch->bo, I915_GEM_DOMAIN_RENDER | I915_GEM_DOMAIN_INSTRUCTION,
		  0, BASE_ADDRESS_MODIFY);
	OUT_BATCH(0 | BASE_ADDRESS_MODIFY);
	OUT_RELOC(batch->bo, I915_GEM_DOMAIN_INSTRUCTION, 0, BASE_ADDRESS_MODIFY);

	OUT_BATCH(0 | BASE_ADDRESS_MODIFY);
	OUT_BATCH(0xfffff000 | BASE_ADDRESS_MODIFY); // copied from mesa
	OUT_BATCH(0 | BASE_ADDRESS_MODIFY);
	OUT_BATCH(0 | BASE_ADDRESS_MODIFY);
}

static void
gen7_emit_urb(struct intel_batchbuffer *batch) {
	/* XXX: Min valid values from mesa */
	const int vs_entries = 32;
	const int vs_size = 2;
	const int vs_start = 2;

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
gen7_emit_cc(struct intel_batchbuffer *batch) {
	OUT_BATCH(GEN7_3DSTATE_BLEND_STATE_POINTERS);
	OUT_BATCH(cc.blend_state | 1);

	OUT_BATCH(GEN6_3DSTATE_CC_STATE_POINTERS);
	OUT_BATCH(cc.cc_state | 1);

	OUT_BATCH(GEN7_3DSTATE_DS_STATE_POINTERS);
	OUT_BATCH(cc.ds_state | 1);
}

static void
gen7_emit_multisample(struct intel_batchbuffer *batch) {
	OUT_BATCH(GEN6_3DSTATE_MULTISAMPLE | 2);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN6_3DSTATE_SAMPLE_MASK);
	OUT_BATCH(1);
}

static void
gen7_emit_vs(struct intel_batchbuffer *batch) {
	OUT_BATCH(GEN7_3DSTATE_BINDING_TABLE_POINTERS_VS);
	OUT_BATCH(0);

	OUT_BATCH(GEN7_3DSTATE_SAMPLER_STATE_POINTERS_VS);
	OUT_BATCH(0);

	OUT_BATCH(GEN6_3DSTATE_CONSTANT_VS | (7-2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN6_3DSTATE_VS | (6-2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
}

static void
gen7_emit_hs(struct intel_batchbuffer *batch) {
	OUT_BATCH(GEN7_3DSTATE_CONSTANT_HS | (7-2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN7_3DSTATE_HS | (7-2));
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
gen7_emit_gs(struct intel_batchbuffer *batch) {
	OUT_BATCH(GEN7_3DSTATE_CONSTANT_GS | (7-2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN7_3DSTATE_GS | (7-2));
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
gen7_emit_ds(struct intel_batchbuffer *batch) {
	OUT_BATCH(GEN7_3DSTATE_CONSTANT_DS | (7-2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN7_3DSTATE_DS | (6-2));
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
gen7_emit_null_state(struct intel_batchbuffer *batch) {
	gen7_emit_hs(batch);
	OUT_BATCH(GEN7_3DSTATE_TE | (4-2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	gen7_emit_gs(batch);
	gen7_emit_ds(batch);
	gen7_emit_vs(batch);
}

static void
gen7_emit_clip(struct intel_batchbuffer *batch) {
	OUT_BATCH(GEN6_3DSTATE_CLIP | (4 - 2));
	OUT_BATCH(0); 
	OUT_BATCH(0); /*  pass-through */
	OUT_BATCH(0);
}

static void
gen7_emit_sf(struct intel_batchbuffer *batch) {
	OUT_BATCH(GEN7_3DSTATE_SBE | (14 - 2));
#ifdef GPU_HANG
	OUT_BATCH(0 << 22 | 1 << 11 | 1 << 4);
#else
	OUT_BATCH(1 << 22 | 1 << 11 | 1 << 4);
#endif
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
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN6_3DSTATE_SF | (7 - 2));
	OUT_BATCH(0);
	OUT_BATCH(GEN6_3DSTATE_SF_CULL_NONE);
//	OUT_BATCH(2 << GEN6_3DSTATE_SF_TRIFAN_PROVOKE_SHIFT);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
}

static void
gen7_emit_ps(struct intel_batchbuffer *batch, uint32_t kernel) {
	const int max_threads = 86;

	OUT_BATCH(GEN6_3DSTATE_WM | (3 - 2));
	OUT_BATCH(GEN7_WM_DISPATCH_ENABLE |
		  /* XXX: I don't understand the BARYCENTRIC stuff, but it
		   * appears we need it to put our setup data in the place we
		   * expect (g6, see below) */
		  GEN7_3DSTATE_PS_PERSPECTIVE_PIXEL_BARYCENTRIC);
	OUT_BATCH(0);

	OUT_BATCH(GEN6_3DSTATE_CONSTANT_PS | (7-2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN7_3DSTATE_PS | (8-2));
	OUT_BATCH(kernel);
	OUT_BATCH(1 << GEN6_3DSTATE_WM_SAMPLER_COUNT_SHITF |
		  2 << GEN6_3DSTATE_WM_BINDING_TABLE_ENTRY_COUNT_SHIFT);
	OUT_BATCH(0); /* scratch space stuff */
	if (IS_HASWELL(batch->devid)) {
		OUT_BATCH((max_threads - 1) << GEN7_3DSTATE_WM_MAX_THREADS_SHIFT |
			  GEN7_3DSTATE_PS_ATTRIBUTE_ENABLED |
			  GEN6_3DSTATE_WM_16_DISPATCH_ENABLE);
	} else {
		OUT_BATCH((max_threads - 1) << HSW_3DSTATE_WM_MAX_THREADS_SHIFT |
			  GEN7_3DSTATE_PS_ATTRIBUTE_ENABLED |
			  GEN6_3DSTATE_WM_16_DISPATCH_ENABLE);
	}
	OUT_BATCH(6 << GEN6_3DSTATE_WM_DISPATCH_START_GRF_0_SHIFT);
	OUT_BATCH(0); // kernel 1
	OUT_BATCH(0); // kernel 2
}

static void
gen7_emit_depth(struct intel_batchbuffer *batch) {
	OUT_BATCH(GEN7_3DSTATE_DEPTH_BUFFER | (7-2));
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN7_3DSTATE_HIER_DEPTH_BUFFER | (3-2));
	OUT_BATCH(0);
	OUT_BATCH(0);

	OUT_BATCH(GEN7_3DSTATE_STENCIL_BUFFER | (3-2));
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
gen6_emit_drawing_rectangle(struct intel_batchbuffer *batch, struct scratch_buf *dst)
{
	OUT_BATCH(GEN6_3DSTATE_DRAWING_RECTANGLE | (4 - 2));
	OUT_BATCH(0);
	OUT_BATCH((buf_height(dst) - 1) << 16 | (buf_width(dst) - 1));
	OUT_BATCH(0);
}

/* Vertex elements MUST be defined before this according to spec */
static void gen7_emit_primitive(struct intel_batchbuffer *batch, uint32_t offset)
{
	OUT_BATCH(GEN6_3DPRIMITIVE | (7-2));
	OUT_BATCH(_3DPRIM_RECTLIST);
	OUT_BATCH(3);	/* vertex count */
	OUT_BATCH(0);	/*  We're specifying this instead with offset in GEN6_3DSTATE_VERTEX_BUFFERS */
	OUT_BATCH(1);	/* single instance */
	OUT_BATCH(0);	/* start instance location */
	OUT_BATCH(0);	/* index buffer offset, ignored */
}

/* The general rule is if it's named gen6 it is directly copied from
 * gen6_render_copyfunc.
 *
 * This sets up most of the 3d pipeline, and most of that to NULL state. The
 * docs aren't specific about exactly what must be set up NULL, but the general
 * rule is we could be run at any time, and so the most state we set to NULL,
 * the better our odds of success.
 *
 * +---------------+ <---- 4096
 * |       ^       |
 * |       |       |
 * |    various    |
 * |      state    |
 * |       |       |
 * |_______|_______| <---- 2048 + ?
 * |       ^       |
 * |       |       |
 * |   batch       |
 * |    commands   |
 * |       |       |
 * |       |       |
 * +---------------+ <---- 0 + ?
 *
 * The batch commands point to state within tthe batch, so all state offsets should be
 * 0 < offset < 4096. Both commands and state build upwards, and are constructed
 * in that order. This means too many batch commands can delete state if not
 * careful.
 *
 */

#define BATCH_STATE_SPLIT 2048
void gen7_render_copyfunc(struct intel_batchbuffer *batch,
			  struct scratch_buf *src, unsigned src_x, unsigned src_y,
			  unsigned width, unsigned height,
			  struct scratch_buf *dst, unsigned dst_x, unsigned dst_y)
{
	uint32_t ps_sampler_state, ps_kernel_off, ps_binding_table;
	uint32_t scissor_state;
	uint32_t vertex_buffer;
	uint32_t batch_end;

	intel_batchbuffer_flush(batch);

	batch_align(batch, 8);

	batch->ptr = &batch->buffer[BATCH_STATE_SPLIT];

	ps_binding_table  = gen7_bind_surfaces(batch, src, dst);
	ps_sampler_state  = gen7_create_sampler(batch);
	ps_kernel_off = batch_copy(batch, ps_kernel, sizeof(ps_kernel), 64);
	vertex_buffer = gen7_fill_vertex_buffer_data(batch, src, src_x, src_y, dst_x, dst_y, width, height);
	cc.cc_state = gen6_create_cc_state(batch);
	cc.ds_state = gen6_create_depth_stencil_state(batch);
	cc.blend_state = gen6_create_blend_state(batch);
	viewport.cc_state = gen6_create_cc_viewport(batch);
	viewport.sf_clip_state = gen7_create_sf_clip_viewport(batch);
	scissor_state = gen6_create_scissor_rect(batch);
	/* TODO: theree is other state which isn't setup */

	assert(batch->ptr < &batch->buffer[4095]);

	batch->ptr = batch->buffer;

	/* Start emitting the commands. The order roughly follows the mesa blorp
	 * order */
	OUT_BATCH(GEN6_PIPELINE_SELECT | PIPELINE_SELECT_3D);

	gen6_emit_sip(batch);

	gen7_emit_push_constants(batch);

	gen7_emit_state_base_address(batch);

	OUT_BATCH(GEN7_3DSTATE_VIEWPORT_STATE_POINTERS_CC);
	OUT_BATCH(viewport.cc_state);
	OUT_BATCH(GEN7_3DSTATE_VIEWPORT_STATE_POINTERS_SF_CLIP);
	OUT_BATCH(viewport.sf_clip_state);

	gen7_emit_urb(batch);

	gen7_emit_cc(batch);

	gen7_emit_multisample(batch);

	gen7_emit_null_state(batch);

	OUT_BATCH(GEN7_3DSTATE_STREAMOUT | 1);
	OUT_BATCH(0);
	OUT_BATCH(0);

	gen7_emit_clip(batch);

	gen7_emit_sf(batch);

	OUT_BATCH(GEN7_3DSTATE_BINDING_TABLE_POINTERS_PS);
	OUT_BATCH(ps_binding_table);

	OUT_BATCH(GEN7_3DSTATE_SAMPLER_STATE_POINTERS_PS);
	OUT_BATCH(ps_sampler_state);

	gen7_emit_ps(batch, ps_kernel_off);

	OUT_BATCH(GEN6_3DSTATE_SCISSOR_STATE_POINTERS);
	OUT_BATCH(scissor_state);

	gen7_emit_depth(batch);

	gen7_emit_clear(batch);

	gen6_emit_drawing_rectangle(batch, dst);

	gen7_emit_vertex_buffer(batch, vertex_buffer);
	gen6_emit_vertex_elements(batch);

	gen7_emit_primitive(batch, vertex_buffer);

	OUT_BATCH(MI_BATCH_BUFFER_END);

	batch_end = batch_align(batch, 8);
	assert(batch_end < BATCH_STATE_SPLIT);

	dump_batch(batch);

	gen6_render_flush(batch, batch_end);
	intel_batchbuffer_reset(batch);
}

#if DEBUG_RENDERCPY
static void dump_batch(struct intel_batchbuffer *batch) {
	int fd = open("/tmp/i965-batchbuffers.dump", O_WRONLY | O_CREAT,  0666);
	if (fd != -1) {
		write(fd, batch->buffer, 4096);
		fd = close(fd);
	}
}
#endif

#include <intel_bufmgr.h>
#include <i915_drm.h>

#include "media_fill.h"
#include "gen8_media.h"
#include "intel_reg.h"
#include "drmtest.h"

#include <assert.h>


static const uint32_t media_kernel[][4] = {
	{ 0x00400001, 0x20202288, 0x00000020, 0x00000000 },
	{ 0x00600001, 0x20800208, 0x008d0000, 0x00000000 },
	{ 0x00200001, 0x20800208, 0x00450040, 0x00000000 },
	{ 0x00000001, 0x20880608, 0x00000000, 0x000f000f },
	{ 0x00800001, 0x20a00208, 0x00000020, 0x00000000 },
	{ 0x00800001, 0x20e00208, 0x00000020, 0x00000000 },
	{ 0x00800001, 0x21200208, 0x00000020, 0x00000000 },
	{ 0x00800001, 0x21600208, 0x00000020, 0x00000000 },
	{ 0x0c800031, 0x24000a40, 0x0e000080, 0x120a8000 },
	{ 0x00600001, 0x2e000208, 0x008d0000, 0x00000000 },
	{ 0x07800031, 0x20000a40, 0x0e000e00, 0x82000010 },
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
gen8_render_flush(struct intel_batchbuffer *batch, uint32_t batch_end)
{
	int ret;

	ret = drm_intel_bo_subdata(batch->bo, 0, 4096, batch->buffer);
	if (ret == 0)
		ret = drm_intel_bo_mrb_exec(batch->bo, batch_end,
					NULL, 0, 0, 0);
	igt_assert(ret == 0);
}

static uint32_t
gen8_fill_curbe_buffer_data(struct intel_batchbuffer *batch,
			uint8_t color)
{
	uint8_t *curbe_buffer;
	uint32_t offset;

	curbe_buffer = batch_alloc(batch, sizeof(uint32_t) * 8, 64);
	offset = batch_offset(batch, curbe_buffer);
	*curbe_buffer = color;

	return offset;
}

static uint32_t
gen8_fill_surface_state(struct intel_batchbuffer *batch,
			struct igt_buf *buf,
			uint32_t format,
			int is_dst)
{
	struct gen8_surface_state *ss;
	uint32_t write_domain, read_domain, offset;
	int ret;

	if (is_dst) {
		write_domain = read_domain = I915_GEM_DOMAIN_RENDER;
	} else {
		write_domain = 0;
		read_domain = I915_GEM_DOMAIN_SAMPLER;
	}

	ss = batch_alloc(batch, sizeof(*ss), 64);
	offset = batch_offset(batch, ss);

	ss->ss0.surface_type = GEN8_SURFACE_2D;
	ss->ss0.surface_format = format;
	ss->ss0.render_cache_read_write = 1;
	ss->ss0.vertical_alignment = 1; /* align 4 */
	ss->ss0.horizontal_alignment = 1; /* align 4 */

	if (buf->tiling == I915_TILING_X)
		ss->ss0.tiled_mode = 2;
	else if (buf->tiling == I915_TILING_Y)
		ss->ss0.tiled_mode = 3;

	ss->ss8.base_addr = buf->bo->offset;

	ret = drm_intel_bo_emit_reloc(batch->bo,
				batch_offset(batch, ss) + 8 * 4,
				buf->bo, 0,
				read_domain, write_domain);
	igt_assert(ret == 0);

	ss->ss2.height = igt_buf_height(buf) - 1;
	ss->ss2.width  = igt_buf_width(buf) - 1;
	ss->ss3.pitch  = buf->stride - 1;

	ss->ss7.shader_chanel_select_r = 4;
	ss->ss7.shader_chanel_select_g = 5;
	ss->ss7.shader_chanel_select_b = 6;
	ss->ss7.shader_chanel_select_a = 7;

	return offset;
}

static uint32_t
gen8_fill_binding_table(struct intel_batchbuffer *batch,
			struct igt_buf *dst)
{
	uint32_t *binding_table, offset;

	binding_table = batch_alloc(batch, 32, 64);
	offset = batch_offset(batch, binding_table);

	binding_table[0] = gen8_fill_surface_state(batch, dst, GEN8_SURFACEFORMAT_R8_UNORM, 1);

	return offset;
}

static uint32_t
gen8_fill_media_kernel(struct intel_batchbuffer *batch,
		const uint32_t kernel[][4],
		size_t size)
{
	uint32_t offset;

	offset = batch_copy(batch, kernel, size, 64);

	return offset;
}

static uint32_t
gen8_fill_interface_descriptor(struct intel_batchbuffer *batch, struct igt_buf *dst)
{
	struct gen8_interface_descriptor_data *idd;
	uint32_t offset;
	uint32_t binding_table_offset, kernel_offset;

	binding_table_offset = gen8_fill_binding_table(batch, dst);
	kernel_offset = gen8_fill_media_kernel(batch, media_kernel, sizeof(media_kernel));

	idd = batch_alloc(batch, sizeof(*idd), 64);
	offset = batch_offset(batch, idd);

	idd->desc0.kernel_start_pointer = (kernel_offset >> 6);

	idd->desc2.single_program_flow = 1;
	idd->desc2.floating_point_mode = GEN8_FLOATING_POINT_IEEE_754;

	idd->desc3.sampler_count = 0;      /* 0 samplers used */
	idd->desc3.sampler_state_pointer = 0;

	idd->desc4.binding_table_entry_count = 0;
	idd->desc4.binding_table_pointer = (binding_table_offset >> 5);

	idd->desc5.constant_urb_entry_read_offset = 0;
	idd->desc5.constant_urb_entry_read_length = 1; /* grf 1 */

	return offset;
}

static void
gen8_emit_state_base_address(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN8_STATE_BASE_ADDRESS | (16 - 2));

	/* general */
	OUT_BATCH(0 | BASE_ADDRESS_MODIFY);
	OUT_BATCH(0);

	/* stateless data port */
	OUT_BATCH(0 | BASE_ADDRESS_MODIFY);

	/* surface */
	OUT_RELOC(batch->bo, I915_GEM_DOMAIN_SAMPLER, 0, BASE_ADDRESS_MODIFY);
	OUT_BATCH(0);

	/* dynamic */
	OUT_RELOC(batch->bo, I915_GEM_DOMAIN_RENDER | I915_GEM_DOMAIN_INSTRUCTION,
		0, BASE_ADDRESS_MODIFY);
	OUT_BATCH(0);

	/* indirect */
	OUT_BATCH(0);
	OUT_BATCH(0);

	/* instruction */
	OUT_RELOC(batch->bo, I915_GEM_DOMAIN_INSTRUCTION, 0, BASE_ADDRESS_MODIFY);
	OUT_BATCH(0);

	/* general state buffer size */
	OUT_BATCH(0xfffff000 | 1);
	/* dynamic state buffer size */
	OUT_BATCH(1 << 12 | 1);
	/* indirect object buffer size */
	OUT_BATCH(0xfffff000 | 1);
	/* intruction buffer size, must set modify enable bit, otherwise it may result in GPU hang */
	OUT_BATCH(1 << 12 | 1);
}

static void
gen8_emit_vfe_state(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN8_MEDIA_VFE_STATE | (9 - 2));

	/* scratch buffer */
	OUT_BATCH(0);
	OUT_BATCH(0);

	/* number of threads & urb entries */
	OUT_BATCH(1 << 16 |
		2 << 8);

	OUT_BATCH(0);

	/* urb entry size & curbe size */
	OUT_BATCH(2 << 16 |
		2);

	/* scoreboard */
	OUT_BATCH(0);
	OUT_BATCH(0);
	OUT_BATCH(0);
}

static void
gen8_emit_curbe_load(struct intel_batchbuffer *batch, uint32_t curbe_buffer)
{
	OUT_BATCH(GEN8_MEDIA_CURBE_LOAD | (4 - 2));
	OUT_BATCH(0);
	/* curbe total data length */
	OUT_BATCH(64);
	/* curbe data start address, is relative to the dynamics base address */
	OUT_BATCH(curbe_buffer);
}

static void
gen8_emit_interface_descriptor_load(struct intel_batchbuffer *batch, uint32_t interface_descriptor)
{
	OUT_BATCH(GEN8_MEDIA_INTERFACE_DESCRIPTOR_LOAD | (4 - 2));
	OUT_BATCH(0);
	/* interface descriptor data length */
	OUT_BATCH(sizeof(struct gen8_interface_descriptor_data));
	/* interface descriptor address, is relative to the dynamics base address */
	OUT_BATCH(interface_descriptor);
}

static void
gen8_emit_media_state_flush(struct intel_batchbuffer *batch)
{
	OUT_BATCH(GEN8_MEDIA_STATE_FLUSH | (2 - 2));
	OUT_BATCH(0);
}

static void
gen8_emit_media_objects(struct intel_batchbuffer *batch,
			unsigned x, unsigned y,
			unsigned width, unsigned height)
{
	int i, j;

	for (i = 0; i < width / 16; i++) {
		for (j = 0; j < height / 16; j++) {
			OUT_BATCH(GEN8_MEDIA_OBJECT | (8 - 2));

			/* interface descriptor offset */
			OUT_BATCH(0);

			/* without indirect data */
			OUT_BATCH(0);
			OUT_BATCH(0);

			/* scoreboard */
			OUT_BATCH(0);
			OUT_BATCH(0);

			/* inline data (xoffset, yoffset) */
			OUT_BATCH(x + i * 16);
			OUT_BATCH(y + j * 16);
			gen8_emit_media_state_flush(batch);
		}
	}
}

/*
 * This sets up the media pipeline,
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
 */

#define BATCH_STATE_SPLIT 2048

void
gen8_media_fillfunc(struct intel_batchbuffer *batch,
		struct igt_buf *dst,
		unsigned x, unsigned y,
		unsigned width, unsigned height,
		uint8_t color)
{
	uint32_t curbe_buffer, interface_descriptor;
	uint32_t batch_end;

	intel_batchbuffer_flush(batch);

	/* setup states */
	batch->ptr = &batch->buffer[BATCH_STATE_SPLIT];

	curbe_buffer = gen8_fill_curbe_buffer_data(batch, color);
	interface_descriptor = gen8_fill_interface_descriptor(batch, dst);
	igt_assert(batch->ptr < &batch->buffer[4095]);

	/* media pipeline */
	batch->ptr = batch->buffer;
	OUT_BATCH(GEN8_PIPELINE_SELECT | PIPELINE_SELECT_MEDIA);
	gen8_emit_state_base_address(batch);

	gen8_emit_vfe_state(batch);

	gen8_emit_curbe_load(batch, curbe_buffer);

	gen8_emit_interface_descriptor_load(batch, interface_descriptor);

	gen8_emit_media_objects(batch, x, y, width, height);

	OUT_BATCH(MI_BATCH_BUFFER_END);

	batch_end = batch_align(batch, 8);
	igt_assert(batch_end < BATCH_STATE_SPLIT);

	gen8_render_flush(batch, batch_end);
	intel_batchbuffer_reset(batch);
}

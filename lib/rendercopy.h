#include "intel_batchbuffer.h"


static inline void emit_vertex_2s(struct intel_batchbuffer *batch,
				  int16_t x, int16_t y)
{
	OUT_BATCH((uint16_t)y << 16 | (uint16_t)x);
}

static inline void emit_vertex(struct intel_batchbuffer *batch,
			       float f)
{
	union { float f; uint32_t ui; } u;
	u.f = f;
	OUT_BATCH(u.ui);
}

static inline void emit_vertex_normalized(struct intel_batchbuffer *batch,
					  float f, float total)
{
	union { float f; uint32_t ui; } u;
	u.f = f / total;
	OUT_BATCH(u.ui);
}

void gen8_render_copyfunc(struct intel_batchbuffer *batch,
			  drm_intel_context *context,
			  struct igt_buf *src, unsigned src_x, unsigned src_y,
			  unsigned width, unsigned height,
			  struct igt_buf *dst, unsigned dst_x, unsigned dst_y);
void gen7_render_copyfunc(struct intel_batchbuffer *batch,
			  drm_intel_context *context,
			  struct igt_buf *src, unsigned src_x, unsigned src_y,
			  unsigned width, unsigned height,
			  struct igt_buf *dst, unsigned dst_x, unsigned dst_y);
void gen6_render_copyfunc(struct intel_batchbuffer *batch,
			  drm_intel_context *context,
			  struct igt_buf *src, unsigned src_x, unsigned src_y,
			  unsigned width, unsigned height,
			  struct igt_buf *dst, unsigned dst_x, unsigned dst_y);
void gen3_render_copyfunc(struct intel_batchbuffer *batch,
			  drm_intel_context *context,
			  struct igt_buf *src, unsigned src_x, unsigned src_y,
			  unsigned width, unsigned height,
			  struct igt_buf *dst, unsigned dst_x, unsigned dst_y);
void gen2_render_copyfunc(struct intel_batchbuffer *batch,
			  drm_intel_context *context,
			  struct igt_buf *src, unsigned src_x, unsigned src_y,
			  unsigned width, unsigned height,
			  struct igt_buf *dst, unsigned dst_x, unsigned dst_y);

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
#include <getopt.h>
#include "drm.h"
#include "i915_drm.h"
#include "drmtest.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_gpu_tools.h"

struct scratch_buf {
    drm_intel_bo *bo;
    uint32_t stride;
    uint32_t tiling;
    uint32_t *data;
    uint32_t *cpu_mapping;
    uint32_t size;
    unsigned num_tiles;
};

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

static inline unsigned buf_width(struct scratch_buf *buf)
{
	return buf->stride/sizeof(uint32_t);
}

static inline unsigned buf_height(struct scratch_buf *buf)
{
	return buf->size/buf->stride;
}

typedef void (*render_copyfunc_t)(struct intel_batchbuffer *batch,
				  struct scratch_buf *src, unsigned src_x, unsigned src_y,
				  unsigned width, unsigned height,
				  struct scratch_buf *dst, unsigned dst_x, unsigned dst_y);

void gen7_render_copyfunc(struct intel_batchbuffer *batch,
			  struct scratch_buf *src, unsigned src_x, unsigned src_y,
			  unsigned width, unsigned height,
			  struct scratch_buf *dst, unsigned dst_x, unsigned dst_y);
void gen6_render_copyfunc(struct intel_batchbuffer *batch,
			  struct scratch_buf *src, unsigned src_x, unsigned src_y,
			  unsigned width, unsigned height,
			  struct scratch_buf *dst, unsigned dst_x, unsigned dst_y);
void gen3_render_copyfunc(struct intel_batchbuffer *batch,
			  struct scratch_buf *src, unsigned src_x, unsigned src_y,
			  unsigned width, unsigned height,
			  struct scratch_buf *dst, unsigned dst_x, unsigned dst_y);
void gen2_render_copyfunc(struct intel_batchbuffer *batch,
			  struct scratch_buf *src, unsigned src_x, unsigned src_y,
			  unsigned width, unsigned height,
			  struct scratch_buf *dst, unsigned dst_x, unsigned dst_y);

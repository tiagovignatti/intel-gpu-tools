/**************************************************************************
 * 
 * Copyright 2006 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "drm.h"
#include "drmtest.h"
#include "intel_batchbuffer.h"
#include "intel_bufmgr.h"
#include "intel_chipset.h"
#include "intel_reg.h"
#include "rendercopy.h"
#include "media_fill.h"
#include "ioctl_wrappers.h"
#include "media_spin.h"

#include <i915_drm.h>

/**
 * SECTION:intel_batchbuffer
 * @short_description: Batchbuffer and blitter support
 * @title: Batch Buffer
 * @include: intel_batchbuffer.h
 *
 * This library provides some basic support for batchbuffers and using the
 * blitter engine based upon libdrm. A new batchbuffer is allocated with
 * intel_batchbuffer_alloc() and for simple blitter commands submitted with
 * intel_batchbuffer_flush().
 *
 * It also provides some convenient macros to easily emit commands into
 * batchbuffers. All those macros presume that a pointer to a #intel_batchbuffer
 * structure called batch is in scope. The basic macros are #BEGIN_BATCH,
 * #OUT_BATCH, #OUT_RELOC and #ADVANCE_BATCH.
 *
 * Note that this library's header pulls in the [i-g-t core](intel-gpu-tools-i-g-t-core.html)
 * library as a dependency.
 */

/**
 * intel_batchbuffer_reset:
 * @batch: batchbuffer object
 *
 * Resets @batch by allocating a new gem buffer object as backing storage.
 */
void
intel_batchbuffer_reset(struct intel_batchbuffer *batch)
{
	if (batch->bo != NULL) {
		drm_intel_bo_unreference(batch->bo);
		batch->bo = NULL;
	}

	batch->bo = drm_intel_bo_alloc(batch->bufmgr, "batchbuffer",
				       BATCH_SZ, 4096);

	memset(batch->buffer, 0, sizeof(batch->buffer));
	batch->ctx = NULL;

	batch->ptr = batch->buffer;
	batch->end = NULL;
}

/**
 * intel_batchbuffer_alloc:
 * @bufmgr: libdrm buffer manager
 * @devid: pci device id of the drm device
 *
 * Allocates a new batchbuffer object. @devid must be supplied since libdrm
 * doesn't expose it directly.
 *
 * Returns: The allocated and initialized batchbuffer object.
 */
struct intel_batchbuffer *
intel_batchbuffer_alloc(drm_intel_bufmgr *bufmgr, uint32_t devid)
{
	struct intel_batchbuffer *batch = calloc(sizeof(*batch), 1);

	batch->bufmgr = bufmgr;
	batch->devid = devid;
	batch->gen = intel_gen(devid);
	intel_batchbuffer_reset(batch);

	return batch;
}

/**
 * intel_batchbuffer_free:
 * @batch: batchbuffer object
 *
 * Releases all resource of the batchbuffer object @batch.
 */
void
intel_batchbuffer_free(struct intel_batchbuffer *batch)
{
	drm_intel_bo_unreference(batch->bo);
	batch->bo = NULL;
	free(batch);
}

#define CMD_POLY_STIPPLE_OFFSET       0x7906

static unsigned int
flush_on_ring_common(struct intel_batchbuffer *batch, int ring)
{
	unsigned int used = batch->ptr - batch->buffer;

	if (used == 0)
		return 0;

	if (IS_GEN5(batch->devid)) {
		/* emit gen5 w/a without batch space checks - we reserve that
		 * already. */
		*(uint32_t *) (batch->ptr) = CMD_POLY_STIPPLE_OFFSET << 16;
		batch->ptr += 4;
		*(uint32_t *) (batch->ptr) = 0;
		batch->ptr += 4;
	}

	/* Round batchbuffer usage to 2 DWORDs. */
	if ((used & 4) == 0) {
		*(uint32_t *) (batch->ptr) = 0; /* noop */
		batch->ptr += 4;
	}

	/* Mark the end of the buffer. */
	*(uint32_t *)(batch->ptr) = MI_BATCH_BUFFER_END; /* noop */
	batch->ptr += 4;
	return batch->ptr - batch->buffer;
}

/**
 * intel_batchbuffer_flush_on_ring:
 * @batch: batchbuffer object
 * @ring: execbuf ring flag
 *
 * Submits the batch for execution on @ring.
 */
void
intel_batchbuffer_flush_on_ring(struct intel_batchbuffer *batch, int ring)
{
	unsigned int used = flush_on_ring_common(batch, ring);
	drm_intel_context *ctx;

	if (used == 0)
		return;

	do_or_die(drm_intel_bo_subdata(batch->bo, 0, used, batch->buffer));

	batch->ptr = NULL;

	/* XXX bad kernel API */
	ctx = batch->ctx;
	if (ring != I915_EXEC_RENDER)
		ctx = NULL;
	do_or_die(drm_intel_gem_bo_context_exec(batch->bo, ctx, used, ring));

	intel_batchbuffer_reset(batch);
}

void
intel_batchbuffer_set_context(struct intel_batchbuffer *batch,
				     drm_intel_context *context)
{
	batch->ctx = context;
}

/**
 * intel_batchbuffer_flush_with_context:
 * @batch: batchbuffer object
 * @context: libdrm hardware context object
 *
 * Submits the batch for execution on the render engine with the supplied
 * hardware context.
 */
void
intel_batchbuffer_flush_with_context(struct intel_batchbuffer *batch,
				     drm_intel_context *context)
{
	int ret;
	unsigned int used = flush_on_ring_common(batch, I915_EXEC_RENDER);

	if (used == 0)
		return;

	ret = drm_intel_bo_subdata(batch->bo, 0, used, batch->buffer);
	igt_assert(ret == 0);

	batch->ptr = NULL;

	ret = drm_intel_gem_bo_context_exec(batch->bo, context, used,
					    I915_EXEC_RENDER);
	igt_assert(ret == 0);

	intel_batchbuffer_reset(batch);
}

/**
 * intel_batchbuffer_flush:
 * @batch: batchbuffer object
 *
 * Submits the batch for execution on the blitter engine, selecting the right
 * ring depending upon the hardware platform.
 */
void
intel_batchbuffer_flush(struct intel_batchbuffer *batch)
{
	int ring = 0;
	if (HAS_BLT_RING(batch->devid))
		ring = I915_EXEC_BLT;
	intel_batchbuffer_flush_on_ring(batch, ring);
}


/**
 * intel_batchbuffer_emit_reloc:
 * @batch: batchbuffer object
 * @buffer: relocation target libdrm buffer object
 * @delta: delta value to add to @buffer's gpu address
 * @read_domains: gem domain bits for the relocation
 * @write_domain: gem domain bit for the relocation
 * @fenced: whether this gpu access requires fences
 *
 * Emits both a libdrm relocation entry pointing at @buffer and the pre-computed
 * DWORD of @batch's presumed gpu address plus the supplied @delta into @batch.
 *
 * Note that @fenced is only relevant if @buffer is actually tiled.
 *
 * This is the only way buffers get added to the validate list.
 */
void
intel_batchbuffer_emit_reloc(struct intel_batchbuffer *batch,
                             drm_intel_bo *buffer, uint64_t delta,
			     uint32_t read_domains, uint32_t write_domain,
			     int fenced)
{
	uint64_t offset;
	int ret;

	if (batch->ptr - batch->buffer > BATCH_SZ)
		igt_info("bad relocation ptr %p map %p offset %d size %d\n",
			 batch->ptr, batch->buffer,
			 (int)(batch->ptr - batch->buffer), BATCH_SZ);

	if (fenced)
		ret = drm_intel_bo_emit_reloc_fence(batch->bo, batch->ptr - batch->buffer,
						    buffer, delta,
						    read_domains, write_domain);
	else
		ret = drm_intel_bo_emit_reloc(batch->bo, batch->ptr - batch->buffer,
					      buffer, delta,
					      read_domains, write_domain);

	offset = buffer->offset64;
	offset += delta;
	intel_batchbuffer_emit_dword(batch, offset);
	if (batch->gen >= 8)
		intel_batchbuffer_emit_dword(batch, offset >> 32);
	igt_assert(ret == 0);
}

/**
 * intel_batchbuffer_data:
 * @batch: batchbuffer object
 * @data: pointer to the data to write into the batchbuffer
 * @bytes: number of bytes to write into the batchbuffer
 *
 * This transfers the given @data into the batchbuffer. Note that the length
 * must be DWORD aligned, i.e. multiples of 32bits.
 */
void
intel_batchbuffer_data(struct intel_batchbuffer *batch,
                       const void *data, unsigned int bytes)
{
	igt_assert((bytes & 3) == 0);
	intel_batchbuffer_require_space(batch, bytes);
	memcpy(batch->ptr, data, bytes);
	batch->ptr += bytes;
}

/**
 * intel_blt_copy:
 * @batch: batchbuffer object
 * @src_bo: source libdrm buffer object
 * @src_x1: source pixel x-coordination
 * @src_y1: source pixel y-coordination
 * @src_pitch: @src_bo's pitch in bytes
 * @dst_bo: destination libdrm buffer object
 * @dst_x1: destination pixel x-coordination
 * @dst_y1: destination pixel y-coordination
 * @dst_pitch: @dst_bo's pitch in bytes
 * @width: width of the copied rectangle
 * @height: height of the copied rectangle
 * @bpp: bits per pixel
 *
 * This emits a 2D copy operation using blitter commands into the supplied batch
 * buffer object.
 */
void
intel_blt_copy(struct intel_batchbuffer *batch,
	       drm_intel_bo *src_bo, int src_x1, int src_y1, int src_pitch,
	       drm_intel_bo *dst_bo, int dst_x1, int dst_y1, int dst_pitch,
	       int width, int height, int bpp)
{
	const int gen = batch->gen;
	uint32_t src_tiling, dst_tiling, swizzle;
	uint32_t cmd_bits = 0;
	uint32_t br13_bits;

	igt_assert(bpp*(src_x1 + width) <= 8*src_pitch);
	igt_assert(bpp*(dst_x1 + width) <= 8*dst_pitch);
	igt_assert(src_pitch * (src_y1 + height) <= src_bo->size);
	igt_assert(dst_pitch * (dst_y1 + height) <= dst_bo->size);

	drm_intel_bo_get_tiling(src_bo, &src_tiling, &swizzle);
	drm_intel_bo_get_tiling(dst_bo, &dst_tiling, &swizzle);

	if (gen >= 4 && src_tiling != I915_TILING_NONE) {
		src_pitch /= 4;
		cmd_bits |= XY_SRC_COPY_BLT_SRC_TILED;
	}

	if (gen >= 4 && dst_tiling != I915_TILING_NONE) {
		dst_pitch /= 4;
		cmd_bits |= XY_SRC_COPY_BLT_DST_TILED;
	}

#define CHECK_RANGE(x)	((x) >= 0 && (x) < (1 << 15))
	igt_assert(CHECK_RANGE(src_x1) && CHECK_RANGE(src_y1) &&
		   CHECK_RANGE(dst_x1) && CHECK_RANGE(dst_y1) &&
		   CHECK_RANGE(width) && CHECK_RANGE(height) &&
		   CHECK_RANGE(src_x1 + width) &&
		   CHECK_RANGE(src_y1 + height) &&
		   CHECK_RANGE(dst_x1 + width) &&
		   CHECK_RANGE(dst_y1 + height) &&
		   CHECK_RANGE(src_pitch) &&
		   CHECK_RANGE(dst_pitch));
#undef CHECK_RANGE

	br13_bits = 0;
	switch (bpp) {
	case 8:
		break;
	case 16:		/* supporting only RGB565, not ARGB1555 */
		br13_bits |= 1 << 24;
		break;
	case 32:
		br13_bits |= 3 << 24;
		cmd_bits |= XY_SRC_COPY_BLT_WRITE_ALPHA |
			    XY_SRC_COPY_BLT_WRITE_RGB;
		break;
	default:
		igt_fail(IGT_EXIT_FAILURE);
	}

	BLIT_COPY_BATCH_START(cmd_bits);
	OUT_BATCH((br13_bits) |
		  (0xcc << 16) | /* copy ROP */
		  dst_pitch);
	OUT_BATCH((dst_y1 << 16) | dst_x1); /* dst x1,y1 */
	OUT_BATCH(((dst_y1 + height) << 16) | (dst_x1 + width)); /* dst x2,y2 */
	OUT_RELOC_FENCED(dst_bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
	OUT_BATCH((src_y1 << 16) | src_x1); /* src x1,y1 */
	OUT_BATCH(src_pitch);
	OUT_RELOC_FENCED(src_bo, I915_GEM_DOMAIN_RENDER, 0, 0);
	ADVANCE_BATCH();

#define CMD_POLY_STIPPLE_OFFSET       0x7906
	if (gen == 5) {
		BEGIN_BATCH(2, 0);
		OUT_BATCH(CMD_POLY_STIPPLE_OFFSET << 16);
		OUT_BATCH(0);
		ADVANCE_BATCH();
	}

	if (gen >= 6 && src_bo == dst_bo) {
		BEGIN_BATCH(3, 0);
		OUT_BATCH(XY_SETUP_CLIP_BLT_CMD);
		OUT_BATCH(0);
		OUT_BATCH(0);
		ADVANCE_BATCH();
	}

	intel_batchbuffer_flush(batch);
}

/**
 * intel_copy_bo:
 * @batch: batchbuffer object
 * @src_bo: source libdrm buffer object
 * @dst_bo: destination libdrm buffer object
 * @size: size of the copy range in bytes
 *
 * This emits a copy operation using blitter commands into the supplied batch
 * buffer object. A total of @size bytes from the start of @src_bo is copied
 * over to @dst_bo. Note that @size must be page-aligned.
 */
void
intel_copy_bo(struct intel_batchbuffer *batch,
	      drm_intel_bo *dst_bo, drm_intel_bo *src_bo,
	      long int size)
{
	igt_assert(size % 4096 == 0);

	intel_blt_copy(batch,
		       src_bo, 0, 0, 4096,
		       dst_bo, 0, 0, 4096,
		       4096/4, size/4096, 32);
}

/**
 * igt_buf_width:
 * @buf: the i-g-t buffer object
 *
 * Computes the widht in 32-bit pixels of the given buffer.
 *
 * Returns:
 * The width of the buffer.
 */
unsigned igt_buf_width(struct igt_buf *buf)
{
	return buf->stride/sizeof(uint32_t);
}

/**
 * igt_buf_height:
 * @buf: the i-g-t buffer object
 *
 * Computes the height in 32-bit pixels of the given buffer.
 *
 * Returns:
 * The height of the buffer.
 */
unsigned igt_buf_height(struct igt_buf *buf)
{
	return buf->size/buf->stride;
}

/*
 * pitches are in bytes if the surfaces are linear, number of dwords
 * otherwise
 */
static uint32_t fast_copy_pitch(unsigned int stride, unsigned int tiling)
{
	if (tiling != I915_TILING_NONE)
		return stride / 4;
	else
		return stride;
}

static uint32_t fast_copy_dword0(unsigned int src_tiling,
				 unsigned int dst_tiling)
{
	uint32_t dword0 = 0;

	dword0 |= XY_FAST_COPY_BLT;

	switch (src_tiling) {
	case I915_TILING_X:
		dword0 |= XY_FAST_COPY_SRC_TILING_X;
		break;
	case I915_TILING_Y:
	case I915_TILING_Yf:
		dword0 |= XY_FAST_COPY_SRC_TILING_Yb_Yf;
		break;
	case I915_TILING_Ys:
		dword0 |= XY_FAST_COPY_SRC_TILING_Ys;
		break;
	case I915_TILING_NONE:
	default:
		break;
	}

	switch (dst_tiling) {
	case I915_TILING_X:
		dword0 |= XY_FAST_COPY_DST_TILING_X;
		break;
	case I915_TILING_Y:
	case I915_TILING_Yf:
		dword0 |= XY_FAST_COPY_DST_TILING_Yb_Yf;
		break;
	case I915_TILING_Ys:
		dword0 |= XY_FAST_COPY_DST_TILING_Ys;
		break;
	case I915_TILING_NONE:
	default:
		break;
	}

	return dword0;
}

static uint32_t fast_copy_dword1(unsigned int src_tiling,
				 unsigned int dst_tiling)
{
	uint32_t dword1 = 0;

	if (src_tiling == I915_TILING_Yf)
		dword1 |= XY_FAST_COPY_SRC_TILING_Yf;
	if (dst_tiling == I915_TILING_Yf)
		dword1 |= XY_FAST_COPY_DST_TILING_Yf;

	dword1 |= XY_FAST_COPY_COLOR_DEPTH_32;

	return dword1;
}

static void
fill_relocation(struct drm_i915_gem_relocation_entry *reloc,
		uint32_t gem_handle, uint32_t offset, /* in dwords */
		uint32_t read_domains, uint32_t write_domains)
{
	reloc->target_handle = gem_handle;
	reloc->delta = 0;
	reloc->offset = offset * sizeof(uint32_t);
	reloc->presumed_offset = 0;
	reloc->read_domains = read_domains;
	reloc->write_domain = write_domains;
}

static void
fill_object(struct drm_i915_gem_exec_object2 *obj, uint32_t gem_handle,
	    struct drm_i915_gem_relocation_entry *relocs, uint32_t count)
{
	memset(obj, 0, sizeof(*obj));
	obj->handle = gem_handle;
	obj->relocation_count = count;
	obj->relocs_ptr = (uintptr_t)relocs;
}

static void exec_blit(int fd,
		      struct drm_i915_gem_exec_object2 *objs, uint32_t count,
		      uint32_t batch_len /* in dwords */)
{
	struct drm_i915_gem_execbuffer2 exec;

	exec.buffers_ptr = (uintptr_t)objs;
	exec.buffer_count = count;
	exec.batch_start_offset = 0;
	exec.batch_len = batch_len * 4;
	exec.DR1 = exec.DR4 = 0;
	exec.num_cliprects = 0;
	exec.cliprects_ptr = 0;
	exec.flags = I915_EXEC_BLT;
	i915_execbuffer2_set_context_id(exec, 0);
	exec.rsvd2 = 0;

	gem_execbuf(fd, &exec);
}

/**
 * igt_blitter_fast_copy__raw:
 * @fd: file descriptor of the i915 driver
 * @src_handle: GEM handle of the source buffer
 * @src_stride: Stride (in bytes) of the source buffer
 * @src_tiling: Tiling mode of the source buffer
 * @src_x: X coordinate of the source region to copy
 * @src_y: Y coordinate of the source region to copy
 * @width: Width of the region to copy
 * @height: Height of the region to copy
 * @dst_handle: GEM handle of the source buffer
 * @dst_stride: Stride (in bytes) of the destination buffer
 * @dst_tiling: Tiling mode of the destination buffer
 * @dst_x: X coordinate of destination
 * @dst_y: Y coordinate of destination
 *
 * Like igt_blitter_fast_copy(), but talking to the kernel directly.
 */
void igt_blitter_fast_copy__raw(int fd,
				/* src */
				uint32_t src_handle,
				unsigned int src_stride,
				unsigned int src_tiling,
				unsigned int src_x, unsigned src_y,

				/* size */
				unsigned int width, unsigned int height,

				/* dst */
				uint32_t dst_handle,
				unsigned int dst_stride,
				unsigned int dst_tiling,
				unsigned int dst_x, unsigned dst_y)
{
	uint32_t batch[12];
	struct drm_i915_gem_exec_object2 objs[3];
	struct drm_i915_gem_relocation_entry relocs[2];
	uint32_t batch_handle;
	uint32_t dword0, dword1;
	uint32_t src_pitch, dst_pitch;
	int i = 0;

	src_pitch = fast_copy_pitch(src_stride, src_tiling);
	dst_pitch = fast_copy_pitch(dst_stride, dst_tiling);
	dword0 = fast_copy_dword0(src_tiling, dst_tiling);
	dword1 = fast_copy_dword1(src_tiling, dst_tiling);

#define CHECK_RANGE(x)	((x) >= 0 && (x) < (1 << 15))
	assert(CHECK_RANGE(src_x) && CHECK_RANGE(src_y) &&
	       CHECK_RANGE(dst_x) && CHECK_RANGE(dst_y) &&
	       CHECK_RANGE(width) && CHECK_RANGE(height) &&
	       CHECK_RANGE(src_x + width) && CHECK_RANGE(src_y + height) &&
	       CHECK_RANGE(dst_x + width) && CHECK_RANGE(dst_y + height) &&
	       CHECK_RANGE(src_pitch) && CHECK_RANGE(dst_pitch));
#undef CHECK_RANGE

	batch[i++] = dword0;
	batch[i++] = dword1 | dst_pitch;
	batch[i++] = (dst_y << 16) | dst_x; /* dst x1,y1 */
	batch[i++] = ((dst_y + height) << 16) | (dst_x + width); /* dst x2,y2 */
	batch[i++] = 0; /* dst address lower bits */
	batch[i++] = 0;	/* dst address upper bits */
	batch[i++] = (src_y << 16) | src_x; /* src x1,y1 */
	batch[i++] = src_pitch;
	batch[i++] = 0; /* src address lower bits */
	batch[i++] = 0;	/* src address upper bits */
	batch[i++] = MI_BATCH_BUFFER_END;
	batch[i++] = MI_NOOP;

	igt_assert(i == ARRAY_SIZE(batch));

	batch_handle = gem_create(fd, 4096);
	gem_write(fd, batch_handle, 0, batch, sizeof(batch));

	fill_relocation(&relocs[0], dst_handle, 4,
			I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER);
	fill_relocation(&relocs[1], src_handle, 8, I915_GEM_DOMAIN_RENDER, 0);

	fill_object(&objs[0], dst_handle, NULL, 0);
	fill_object(&objs[1], src_handle, NULL, 0);
	fill_object(&objs[2], batch_handle, relocs, 2);

	exec_blit(fd, objs, 3, ARRAY_SIZE(batch));

	gem_close(fd, batch_handle);
}

/**
 * igt_blitter_fast_copy:
 * @batch: batchbuffer object
 * @src: source i-g-t buffer object
 * @src_x: source pixel x-coordination
 * @src_y: source pixel y-coordination
 * @width: width of the copied rectangle
 * @height: height of the copied rectangle
 * @dst: destination i-g-t buffer object
 * @dst_x: destination pixel x-coordination
 * @dst_y: destination pixel y-coordination
 *
 * Copy @src into @dst using the gen9 fast copy blitter comamnd.
 *
 * The source and destination surfaces cannot overlap.
 */
void igt_blitter_fast_copy(struct intel_batchbuffer *batch,
			   struct igt_buf *src, unsigned src_x, unsigned src_y,
			   unsigned width, unsigned height,
			   struct igt_buf *dst, unsigned dst_x, unsigned dst_y)
{
	uint32_t src_pitch, dst_pitch;
	uint32_t dword0, dword1;

	src_pitch = fast_copy_pitch(src->stride, src->tiling);
	dst_pitch = fast_copy_pitch(dst->stride, src->tiling);
	dword0 = fast_copy_dword0(src->tiling, dst->tiling);
	dword1 = fast_copy_dword1(src->tiling, dst->tiling);

#define CHECK_RANGE(x)	((x) >= 0 && (x) < (1 << 15))
	assert(CHECK_RANGE(src_x) && CHECK_RANGE(src_y) &&
	       CHECK_RANGE(dst_x) && CHECK_RANGE(dst_y) &&
	       CHECK_RANGE(width) && CHECK_RANGE(height) &&
	       CHECK_RANGE(src_x + width) && CHECK_RANGE(src_y + height) &&
	       CHECK_RANGE(dst_x + width) && CHECK_RANGE(dst_y + height) &&
	       CHECK_RANGE(src_pitch) && CHECK_RANGE(dst_pitch));
#undef CHECK_RANGE

	BEGIN_BATCH(10, 2);
	OUT_BATCH(dword0);
	OUT_BATCH(dword1 | dst_pitch);
	OUT_BATCH((dst_y << 16) | dst_x); /* dst x1,y1 */
	OUT_BATCH(((dst_y + height) << 16) | (dst_x + width)); /* dst x2,y2 */
	OUT_RELOC(dst->bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, 0);
	OUT_BATCH(0);	/* dst address upper bits */
	OUT_BATCH((src_y << 16) | src_x); /* src x1,y1 */
	OUT_BATCH(src_pitch);
	OUT_RELOC(src->bo, I915_GEM_DOMAIN_RENDER, 0, 0);
	OUT_BATCH(0);	/* src address upper bits */
	ADVANCE_BATCH();

	intel_batchbuffer_flush(batch);
}

/**
 * igt_get_render_copyfunc:
 * @devid: pci device id
 *
 * Returns:
 *
 * The platform-specific render copy function pointer for the device
 * specified with @devid. Will return NULL when no render copy function is
 * implemented.
 */
igt_render_copyfunc_t igt_get_render_copyfunc(int devid)
{
	igt_render_copyfunc_t copy = NULL;

	if (IS_GEN2(devid))
		copy = gen2_render_copyfunc;
	else if (IS_GEN3(devid))
		copy = gen3_render_copyfunc;
	else if (IS_GEN6(devid))
		copy = gen6_render_copyfunc;
	else if (IS_GEN7(devid))
		copy = gen7_render_copyfunc;
	else if (IS_GEN8(devid))
		copy = gen8_render_copyfunc;
	else if (IS_GEN9(devid))
		copy = gen9_render_copyfunc;

	return copy;
}

/**
 * igt_get_media_fillfunc:
 * @devid: pci device id
 *
 * Returns:
 *
 * The platform-specific media fill function pointer for the device specified
 * with @devid. Will return NULL when no media fill function is implemented.
 */
igt_fillfunc_t igt_get_media_fillfunc(int devid)
{
	igt_fillfunc_t fill = NULL;

	if (IS_GEN9(devid))
		fill = gen9_media_fillfunc;
	else if (IS_BROADWELL(devid))
		fill = gen8_media_fillfunc;
	else if (IS_GEN7(devid))
		fill = gen7_media_fillfunc;
	else if (IS_CHERRYVIEW(devid))
		fill = gen8lp_media_fillfunc;

	return fill;
}

/**
 * igt_get_gpgpu_fillfunc:
 * @devid: pci device id
 *
 * Returns:
 *
 * The platform-specific gpgpu fill function pointer for the device specified
 * with @devid. Will return NULL when no gpgpu fill function is implemented.
 */
igt_fillfunc_t igt_get_gpgpu_fillfunc(int devid)
{
	igt_fillfunc_t fill = NULL;

	if (IS_GEN7(devid))
		fill = gen7_gpgpu_fillfunc;

	return fill;
}

/**
 * igt_get_media_spinfunc:
 * @devid: pci device id
 *
 * Returns:
 *
 * The platform-specific media spin function pointer for the device specified
 * with @devid. Will return NULL when no media spin function is implemented.
 */
igt_media_spinfunc_t igt_get_media_spinfunc(int devid)
{
	igt_media_spinfunc_t spin = NULL;

	if (IS_GEN9(devid))
		spin = gen9_media_spinfunc;
	else if (IS_BROADWELL(devid))
		spin = gen8_media_spinfunc;
	else if (IS_CHERRYVIEW(devid))
		spin = gen8lp_media_spinfunc;

	return spin;
}

/**************************************************************************
 *
 * Copyright 2006 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Copyright 2014 Intel Corporation
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

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "intel_batchbuffer.h"

int intel_batch_reset(struct intel_batchbuffer *batch,
		      void *p,
		      uint32_t size,
		      uint32_t off)
{
	batch->err = -EINVAL;
	batch->base = batch->base_ptr = p;
	batch->state_base = batch->state_ptr = p;

	if (off >= size || ALIGN(off, 4) != off)
		return -EINVAL;

	batch->size = size;

	batch->state_base = batch->state_ptr = &batch->base[off];

	batch->num_relocs = 0;
	batch->err = 0;

	return batch->err;
}

uint32_t intel_batch_state_used(struct intel_batchbuffer *batch)
{
	return batch->state_ptr - batch->state_base;
}

uint32_t intel_batch_state_offset(struct intel_batchbuffer *batch)
{
	return batch->state_ptr - batch->base;
}

void *intel_batch_state_alloc(struct intel_batchbuffer *batch,
			      uint32_t size,
			      uint32_t align)
{
	uint32_t cur;
	uint32_t offset;

	if (batch->err)
		return NULL;

	cur  = intel_batch_state_offset(batch);
	offset = ALIGN(cur, align);

	if (offset + size > batch->size) {
		batch->err = -ENOSPC;
		return NULL;
	}

	batch->state_ptr = batch->base + offset + size;

	memset(batch->base + cur, 0, size);

	return batch->base + offset;
}

int intel_batch_offset(struct intel_batchbuffer *batch, const void *ptr)
{
	return (uint8_t *)ptr - batch->base;
}

int intel_batch_state_copy(struct intel_batchbuffer *batch,
			   const void *ptr,
			   const uint32_t size,
			   const uint32_t align)
{
	void * const p = intel_batch_state_alloc(batch, size, align);

	if (p == NULL)
		return -1;

	return intel_batch_offset(batch, memcpy(p, ptr, size));
}

uint32_t intel_batch_cmds_used(struct intel_batchbuffer *batch)
{
	return batch->base_ptr - batch->base;
}

uint32_t intel_batch_total_used(struct intel_batchbuffer *batch)
{
	return batch->state_ptr - batch->base;
}

static uint32_t intel_batch_space(struct intel_batchbuffer *batch)
{
	return batch->state_base - batch->base_ptr;
}

int intel_batch_emit_dword(struct intel_batchbuffer *batch, uint32_t dword)
{
	uint32_t offset;

	if (batch->err)
		return -1;

	if (intel_batch_space(batch) < 4) {
		batch->err = -ENOSPC;
		return -1;
	}

	offset = intel_batch_offset(batch, batch->base_ptr);

	*(uint32_t *) (batch->base_ptr) = dword;
	batch->base_ptr += 4;

	return offset;
}

int intel_batch_emit_reloc(struct intel_batchbuffer *batch,
			   const uint32_t delta)
{
	uint32_t offset;

	if (batch->err)
		return -1;

	if (delta >= batch->size) {
		batch->err = -EINVAL;
		return -1;
	}

	offset = intel_batch_emit_dword(batch, delta);

	if (batch->err)
		return -1;

	if (batch->num_relocs >= MAX_RELOCS) {
		batch->err = -ENOSPC;
		return -1;
	}

	batch->relocs[batch->num_relocs++] = offset;

	return offset;
}

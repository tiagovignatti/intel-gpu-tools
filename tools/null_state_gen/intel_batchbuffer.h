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

#ifndef _INTEL_BATCHBUFFER_H
#define _INTEL_BATCHBUFFER_H

#include <stdint.h>

#define MAX_RELOCS 64
#define ALIGN(x, y) (((x) + (y)-1) & ~((y)-1))

struct intel_batchbuffer {
	int err;
	uint8_t *base;
	uint8_t *base_ptr;
	uint8_t *state_base;
	uint8_t *state_ptr;
	int size;

	uint32_t relocs[MAX_RELOCS];
	uint32_t num_relocs;
};

#define OUT_BATCH(d) intel_batch_emit_dword(batch, d)
#define OUT_RELOC(batch, read_domains, write_domain, delta) \
	intel_batch_emit_reloc(batch, delta)

int intel_batch_reset(struct intel_batchbuffer *batch,
		       void *p,
		       uint32_t size, uint32_t split_off);

uint32_t intel_batch_state_used(struct intel_batchbuffer *batch);

void *intel_batch_state_alloc(struct intel_batchbuffer *batch,
			      uint32_t size,
			      uint32_t align);

int intel_batch_offset(struct intel_batchbuffer *batch, const void *ptr);

int intel_batch_state_copy(struct intel_batchbuffer *batch,
			   const void *ptr,
			   const uint32_t size,
			   const uint32_t align);

uint32_t intel_batch_cmds_used(struct intel_batchbuffer *batch);

int intel_batch_emit_dword(struct intel_batchbuffer *batch, uint32_t dword);

int intel_batch_emit_reloc(struct intel_batchbuffer *batch,
			   const uint32_t delta);

uint32_t intel_batch_total_used(struct intel_batchbuffer *batch);

static inline int intel_batch_error(struct intel_batchbuffer *batch)
{
	return batch->err;
}

static inline uint32_t intel_batch_state_start(struct intel_batchbuffer *batch)
{
	return batch->state_base - batch->base;
}

#endif

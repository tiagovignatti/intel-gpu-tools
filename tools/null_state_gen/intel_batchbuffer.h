/**************************************************************************
 *
 * Copyright 2006 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Copyright 2014, 2015 Intel Corporation
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
#define MAX_ITEMS 1024
#define MAX_STRLEN 256

#define ALIGN(x, y) (((x) + (y)-1) & ~((y)-1))

typedef enum {
	UNINITIALIZED,
	CMD,
	STATE,
	RELOC,
	RELOC_STATE,
	STATE_OFFSET,
	PAD,
} item_type;

struct bb_item {
	uint32_t data;
	item_type type;
	char str[MAX_STRLEN];
};

struct bb_area {
	struct bb_item item[MAX_ITEMS];
	unsigned long num_items;
};

struct intel_batchbuffer {
	struct bb_area *cmds;
	struct bb_area *state;
	unsigned long cmds_end_offset;
	unsigned long state_start_offset;
};

struct intel_batchbuffer *intel_batchbuffer_create(void);

#define OUT_CMD_B(cmd, len, bias) intel_batch_cmd_emit_null(batch, (cmd), (len), (bias), #cmd " " #len)
#define OUT_CMD(cmd, len) OUT_CMD_B(cmd, len, 2)

#define OUT_BATCH(d) bb_area_emit(batch->cmds, d, CMD, #d)
#define OUT_BATCH_STATE_OFFSET(d) bb_area_emit(batch->cmds, d, STATE_OFFSET, #d)
#define OUT_RELOC(batch, read_domain, write_domain, d) bb_area_emit(batch->cmds, d, RELOC, #d)
#define OUT_RELOC_STATE(batch, read_domain, write_domain, d) bb_area_emit(batch->cmds, d, RELOC_STATE, #d);
#define OUT_STATE(d) bb_area_emit(batch->state, d, STATE, #d)
#define OUT_STATE_OFFSET(offset) bb_area_emit(batch->state, offset, STATE_OFFSET, #offset)
#define OUT_STATE_STRUCT(name, align) intel_batch_state_copy(batch, &name, sizeof(name), align, #name " " #align)

uint32_t intel_batch_state_copy(struct intel_batchbuffer *batch, const void *d, unsigned bytes, unsigned align,
				const char *name);
uint32_t intel_batch_state_alloc(struct intel_batchbuffer *batch, unsigned bytes, unsigned align,
				 const char *name);
uint32_t intel_batch_state_offset(struct intel_batchbuffer *batch, unsigned align);
unsigned intel_batch_num_cmds(struct intel_batchbuffer *batch);
struct bb_item *intel_batch_state_get(struct intel_batchbuffer *batch, unsigned i);
unsigned intel_batch_num_state(struct intel_batchbuffer *batch);

struct bb_item *intel_batch_cmd_get(struct intel_batchbuffer *batch, unsigned i);
int intel_batch_is_reloc(struct intel_batchbuffer *batch, unsigned i);

void intel_batch_relocate_state(struct intel_batchbuffer *batch);

const char *intel_batch_type_as_str(const struct bb_item *item);

void bb_area_emit(struct bb_area *a, uint32_t dword, item_type type, const char *str);
void bb_area_emit_offset(struct bb_area *a, unsigned i, uint32_t dword, item_type type, const char *str);

void intel_batch_cmd_emit_null(struct intel_batchbuffer *batch,
			       const int cmd,
			       const int len, const int len_bias,
			       const char *str);
#endif

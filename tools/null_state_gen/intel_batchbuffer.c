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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include "intel_batchbuffer.h"

void bb_area_emit(struct bb_area *a, uint32_t dword, item_type type, const char *str)
{
	struct bb_item *item;
	assert(a != NULL);
	assert(a->num_items < MAX_ITEMS);
	item = &a->item[a->num_items];

	item->data = dword;
	item->type = type;
	strncpy(item->str, str, MAX_STRLEN);
	item->str[MAX_STRLEN - 1] = 0;

	a->num_items++;
}

void bb_area_emit_offset(struct bb_area *a, unsigned offset, uint32_t dword, item_type type, const char *str)
{
	const unsigned i = offset / 4;
	struct bb_item *item;
	assert(a != NULL);
	assert(a->num_items < MAX_ITEMS);
	assert(i < a->num_items);
	item = &a->item[i];

	item->data = dword;
	item->type = type;
	strncpy(item->str, str, MAX_STRLEN);
	item->str[MAX_STRLEN - 1] = 0;
}

static struct bb_item *bb_area_get(struct bb_area *a, unsigned i)
{
	assert (i < a->num_items);
	return &a->item[i];
}

static unsigned bb_area_items(struct bb_area *a)
{
	return a->num_items;
}

static unsigned long bb_area_used(struct bb_area *a)
{
	assert(a != NULL);
	assert(a->num_items <= MAX_ITEMS);

	return a->num_items * 4;
}

static unsigned long bb_area_room(struct bb_area *a)
{
	assert (a != NULL);
	assert (a->num_items <= MAX_ITEMS);

	return (MAX_ITEMS - a->num_items) * 4;
}

struct intel_batchbuffer *intel_batchbuffer_create(void)
{
	struct intel_batchbuffer *batch;

	batch = calloc(1, sizeof(*batch));
	if (batch == NULL)
		return NULL;

	batch->cmds = calloc(1, sizeof(struct bb_area));
	if (batch->cmds == NULL) {
		free(batch);
		return NULL;
	}

	batch->state = calloc(1, sizeof(struct bb_area));
	if (batch->state == NULL) {
		free(batch->cmds);
		free(batch);
		return NULL;
	}

	batch->state_start_offset = -1;
	batch->cmds_end_offset = -1;

	return batch;
}

static void bb_area_align(struct bb_area *a, unsigned align)
{
	if (align == 0)
		return;

	assert((align % 4) == 0);

	while ((a->num_items * 4) % align != 0)
		bb_area_emit(a, 0, PAD, "align pad");
}

static int reloc_exists(struct intel_batchbuffer *batch, uint32_t offset)
{
	int i;

	for (i = 0; i < batch->cmds->num_items; i++)
		if ((batch->cmds->item[i].type == RELOC ||
		     batch->cmds->item[i].type == RELOC_STATE) &&
		    i * 4 == offset)
			return 1;

	return 0;
}

int intel_batch_is_reloc(struct intel_batchbuffer *batch, unsigned i)
{
	return reloc_exists(batch, i * 4);
}

static void intel_batch_cmd_align(struct intel_batchbuffer *batch, unsigned align)
{
	bb_area_align(batch->cmds, align);
}

static void intel_batch_state_align(struct intel_batchbuffer *batch, unsigned align)
{
	bb_area_align(batch->state, align);
}

unsigned intel_batch_num_cmds(struct intel_batchbuffer *batch)
{
	return bb_area_items(batch->cmds);
}

unsigned intel_batch_num_state(struct intel_batchbuffer *batch)
{
	return bb_area_items(batch->state);
}

struct bb_item *intel_batch_cmd_get(struct intel_batchbuffer *batch, unsigned i)
{
	return bb_area_get(batch->cmds, i);
}

struct bb_item *intel_batch_state_get(struct intel_batchbuffer *batch, unsigned i)
{
	return bb_area_get(batch->state, i);
}

uint32_t intel_batch_state_offset(struct intel_batchbuffer *batch, unsigned align)
{
	intel_batch_state_align(batch, align);
	return bb_area_used(batch->state);
}

uint32_t intel_batch_state_alloc(struct intel_batchbuffer *batch, unsigned bytes, unsigned align,
				 const char *str)
{
	unsigned offset;
	unsigned dwords = bytes/4;
	assert ((bytes % 4) == 0);
	assert (bb_area_room(batch->state) >= bytes);

	offset = intel_batch_state_offset(batch, align);

	while (dwords--)
		bb_area_emit(batch->state, 0, UNINITIALIZED, str);

	return offset;
}

uint32_t intel_batch_state_copy(struct intel_batchbuffer *batch,
				const void *d, unsigned bytes,
				unsigned align,
				const char *str)
{
	unsigned offset;
	unsigned i;
	unsigned dwords = bytes/4;
	assert (d);
	assert ((bytes % 4) == 0);
	assert (bb_area_room(batch->state) >= bytes);

	offset = intel_batch_state_offset(batch, align);

	for (i = 0; i < dwords; i++) {
		char offsetinside[80];
		const uint32_t *s;
		sprintf(offsetinside, "%s: 0x%x", str, i * 4);

		s = (const uint32_t *)(const uint8_t *)d + i;
		bb_area_emit(batch->state, *s, STATE, offsetinside);
	}

	return offset;
}

void intel_batch_relocate_state(struct intel_batchbuffer *batch)
{
	unsigned int i;

	assert (batch->state_start_offset == -1);

	batch->cmds_end_offset = bb_area_used(batch->cmds) - 4;

	/* Hardcoded, could track max align done also */
	intel_batch_cmd_align(batch, 64);

	batch->state_start_offset = bb_area_used(batch->cmds);

	for (i = 0; i < bb_area_items(batch->state); i++) {
		const struct bb_item *s = bb_area_get(batch->state, i);

		bb_area_emit(batch->cmds, s->data, s->type, s->str);
	}

	for (i = 0; i < bb_area_items(batch->cmds); i++) {
		struct bb_item *s = bb_area_get(batch->cmds, i);

		if (s->type == STATE_OFFSET || s->type == RELOC_STATE)
			s->data += batch->state_start_offset;
	}
}

const char *intel_batch_type_as_str(const struct bb_item *item)
{
	switch (item->type) {
	case UNINITIALIZED:
		return "UNINITIALIZED";
	case CMD:
		return "CMD";
	case STATE:
		return "STATE";
	case PAD:
		return "PAD";
	case RELOC:
		return "RELOC";
	case RELOC_STATE:
		return "RELOC_STATE";
	case STATE_OFFSET:
		return "STATE_OFFSET";
	}

	return "UNKNOWN";
}

void intel_batch_cmd_emit_null(struct intel_batchbuffer *batch,
			       const int cmd, const int len, const int len_bias,
			       const char *str)
{
	int i;

	assert(len > 1);
	assert((len - len_bias) >= 0);

	bb_area_emit(batch->cmds, (cmd | (len - len_bias)), CMD, str);

	for (i = len_bias-1; i < len; i++)
		OUT_BATCH(0);
}

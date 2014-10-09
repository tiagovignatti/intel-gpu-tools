/*
 * Copyright © 2014 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *	Mika Kuoppala <mika.kuoppala@intel.com>
 *	Armin Reese <armin.c.reese@intel.com>
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>

#include "intel_batchbuffer.h"

#define STATE_ALIGN 64

extern int gen6_setup_null_render_state(struct intel_batchbuffer *batch);
extern int gen7_setup_null_render_state(struct intel_batchbuffer *batch);
extern int gen8_setup_null_render_state(struct intel_batchbuffer *batch);

static int debug = 0;

static void print_usage(char *s)
{
	fprintf(stderr, "%s: <gen>\n"
		"     gen:     gen to generate for (6,7,8)\n",
	       s);
}

static int print_state(int gen, struct intel_batchbuffer *batch)
{
	int i;

	printf("#include \"intel_renderstate.h\"\n\n");

	printf("static const u32 gen%d_null_state_relocs[] = {\n", gen);
	for (i = 0; i < batch->cmds->num_items; i++) {
		if (intel_batch_is_reloc(batch, i))
			printf("\t0x%08x,\n", i * 4);
	}
	printf("\t%d,\n", -1);
	printf("};\n\n");

	printf("static const u32 gen%d_null_state_batch[] = {\n", gen);
	for (i = 0; i < intel_batch_num_cmds(batch); i++) {
		const struct bb_item *cmd = intel_batch_cmd_get(batch, i);
		printf("\t0x%08x,", cmd->data);

		if (debug)
			printf("\t /* 0x%08x %s '%s' */", i * 4,
			       intel_batch_type_as_str(cmd), cmd->str);

		if (i * 4 == batch->cmds_end_offset)
			printf("\t /* cmds end */");

		if (intel_batch_is_reloc(batch, i))
			printf("\t /* reloc */");

		if (i * 4 == batch->state_start_offset)
			printf("\t /* state start */");

		if (i == intel_batch_num_cmds(batch) - 1)
			printf("\t /* state end */");

		printf("\n");
	}

	printf("};\n\nRO_RENDERSTATE(%d);\n", gen);

	return 0;
}

static int do_generate(int gen)
{
	struct intel_batchbuffer *batch;
	int ret = -EINVAL;
	int (*null_state_gen)(struct intel_batchbuffer *batch) = NULL;

	batch = intel_batchbuffer_create();
	if (batch == NULL)
		return -ENOMEM;

	switch (gen) {
	case 6:
		null_state_gen = gen6_setup_null_render_state;
		break;

	case 7:
		null_state_gen = gen7_setup_null_render_state;
		break;

	case 8:
		null_state_gen = gen8_setup_null_render_state;
		break;
	}

	if (null_state_gen == NULL) {
		printf("no generator found for %d\n", gen);
		return -EINVAL;
	}

	null_state_gen(batch);
	intel_batch_relocate_state(batch);

	ret = print_state(gen, batch);

	return ret;
}

int main(int argc, char *argv[])
{
	if (argc < 2) {
		print_usage(argv[0]);
		return 1;
	}

	if (argc > 2)
		debug = 1;

	return do_generate(atoi(argv[1]));
}

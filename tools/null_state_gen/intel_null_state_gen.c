#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>

#include "intel_batchbuffer.h"

#define STATE_ALIGN 64

extern int gen6_setup_null_render_state(struct intel_batchbuffer *batch);
extern int gen7_setup_null_render_state(struct intel_batchbuffer *batch);
extern int gen8_setup_null_render_state(struct intel_batchbuffer *batch);

static void print_usage(char *s)
{
	fprintf(stderr, "%s: <gen>\n"
		"     gen:     gen to generate for (6,7,8)\n",
	       s);
}

static int is_reloc(struct intel_batchbuffer *batch, uint32_t offset)
{
	int i;

	for (i = 0; i < batch->num_relocs; i++)
		if (batch->relocs[i] == offset)
			return 1;

	return 0;
}

static int print_state(int gen, struct intel_batchbuffer *batch)
{
	int i;

	printf("#include \"intel_renderstate.h\"\n\n");

	printf("static const u32 gen%d_null_state_relocs[] = {\n", gen);
	for (i = 0; i < batch->num_relocs; i++) {
		printf("\t0x%08x,\n", batch->relocs[i]);
	}
	printf("};\n\n");

	printf("static const u32 gen%d_null_state_batch[] = {\n", gen);
	for (i = 0; i < batch->size; i += 4) {
		const uint32_t *p = (void *)batch->base + i;
		printf("\t0x%08x,", *p);

		if (i == intel_batch_cmds_used(batch) - 4)
			printf("\t /* cmds end */");

		if (i == intel_batch_state_start(batch))
			printf("\t /* state start */");


		if (i == intel_batch_state_start(batch) +
		    intel_batch_state_used(batch) - 4)
			printf("\t /* state end */");

		if (is_reloc(batch, i))
			printf("\t /* reloc */");

		printf("\n");
	}
	printf("};\n\nRO_RENDERSTATE(%d);\n", gen);

	return 0;
}

static int do_generate(int gen)
{
	int initial_size = 8192;
	struct intel_batchbuffer batch;
	void *p;
	int ret = -EINVAL;
	uint32_t cmd_len, state_len, size;
	int (*null_state_gen)(struct intel_batchbuffer *batch) = NULL;

	p = malloc(initial_size);
	if (p == NULL)
		return -ENOMEM;

	assert(ALIGN(initial_size/2, STATE_ALIGN) == initial_size/2);

	ret = intel_batch_reset(&batch, p, initial_size, initial_size/2);
	if (ret)
		goto out;

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
		ret = -EINVAL;
		goto out;
	}

	ret = null_state_gen(&batch);
	if (ret < 0)
		goto out;

	cmd_len = intel_batch_cmds_used(&batch);
	state_len = intel_batch_state_used(&batch);

	size = cmd_len + state_len + ALIGN(cmd_len, STATE_ALIGN) - cmd_len;

	ret = intel_batch_reset(&batch, p, size, ALIGN(cmd_len, STATE_ALIGN));
	if (ret)
		goto out;

	ret = null_state_gen(&batch);
	if (ret < 0)
		goto out;

	assert(cmd_len == intel_batch_cmds_used(&batch));
	assert(state_len == intel_batch_state_used(&batch));
	assert(size == ret);

	/* Batch buffer needs to end */
	assert(*(uint32_t *)(p + cmd_len - 4) == (0xA << 23));

	ret = print_state(gen, &batch);
out:
	free(p);

	if (ret < 0)
		return ret;

	return 0;
}

int main(int argc, char *argv[])
{
	if (argc != 2) {
		print_usage(argv[0]);
		return 1;
	}

	return do_generate(atoi(argv[1]));
}

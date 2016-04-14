/*
 * Copyright © 2016 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 */

/** @file gem_mocs_settings.c
 *
 * Check that the MOCs cache settings are valid.
 */

#include "igt.h"
#include "igt_gt.h"

#define MAX_NUMBER_MOCS_REGISTERS	(64)

enum {
	NONE,
	RESET,
	SUSPEND,
	HIBERNATE
};

#define GEN9_LNCFCMOCS0		(0xB020)	/* L3 Cache Control base */
#define GEN9_GFX_MOCS_0		(0xc800)	/* Graphics MOCS base register*/
#define GEN9_MFX0_MOCS_0	(0xc900)	/* Media 0 MOCS base register*/
#define GEN9_MFX1_MOCS_0	(0xcA00)	/* Media 1 MOCS base register*/
#define GEN9_VEBOX_MOCS_0	(0xcB00)	/* Video MOCS base register*/
#define GEN9_BLT_MOCS_0		(0xcc00)	/* Blitter MOCS base register*/

struct mocs_entry {
	uint32_t	control_value;
	uint16_t	l3cc_value;
};

struct mocs_table {
	uint32_t		size;
	const struct mocs_entry	*table;
};

/* The first entries in the MOCS tables are defined by uABI */
static const struct mocs_entry skylake_mocs_table[] = {
	{ 0x00000009, 0x0010 },
	{ 0x00000038, 0x0030 },
	{ 0x0000003b, 0x0030 },
};

static const struct mocs_entry dirty_skylake_mocs_table[] = {
	{ 0x00003FFF, 0x003F }, /* no snoop bit */
	{ 0x00003FFF, 0x003F },
	{ 0x00003FFF, 0x003F },
};

static const struct mocs_entry broxton_mocs_table[] = {
	{ 0x00000009, 0x0010 },
	{ 0x00000038, 0x0030 },
	{ 0x0000003b, 0x0030 },
};

static const struct mocs_entry dirty_broxton_mocs_table[] = {
	{ 0x00007FFF, 0x003F },
	{ 0x00007FFF, 0x003F },
	{ 0x00007FFF, 0x003F },
};

static const uint32_t write_values[] = {
	0xFFFFFFFF,
	0xFFFFFFFF,
	0xFFFFFFFF,
	0xFFFFFFFF
};

static bool get_mocs_settings(int fd, struct mocs_table *table, bool dirty)
{
	uint32_t devid = intel_get_drm_devid(fd);
	bool result = false;

	if (IS_SKYLAKE(devid) || IS_KABYLAKE(devid)) {
		if (dirty) {
			table->size  = ARRAY_SIZE(dirty_skylake_mocs_table);
			table->table = dirty_skylake_mocs_table;
		} else {
			table->size  = ARRAY_SIZE(skylake_mocs_table);
			table->table = skylake_mocs_table;
		}
		result = true;
	} else if (IS_BROXTON(devid)) {
		if (dirty) {
			table->size  = ARRAY_SIZE(dirty_broxton_mocs_table);
			table->table = dirty_broxton_mocs_table;
		} else {
			table->size  = ARRAY_SIZE(broxton_mocs_table);
			table->table = broxton_mocs_table;
		}
		result = true;
	}

	return result;
}

static uint32_t get_engine_base(uint32_t engine)
{
	/* Note we cannot test BSD1 or BSD2 due to limitations of current ANI */
	switch (engine) {
	case I915_EXEC_BSD:	return GEN9_MFX0_MOCS_0;
/*
	case I915_EXEC_BSD1:	return GEN9_MFX0_MOCS_0;
	case I915_EXEC_BSD2:	return GEN9_MFX1_MOCS_0;
*/
	case I915_EXEC_RENDER:	return GEN9_GFX_MOCS_0;
	case I915_EXEC_BLT:	return GEN9_BLT_MOCS_0;
	case I915_EXEC_VEBOX:	return GEN9_VEBOX_MOCS_0;
	default:		return 0;
	}
}

static uint32_t get_mocs_register_value(int fd, uint64_t offset, uint32_t index)
{
	igt_assert(index < MAX_NUMBER_MOCS_REGISTERS);
	return intel_register_read(offset + index * 4);
}

static void test_mocs_control_values(int fd, uint32_t engine)
{
	const uint32_t engine_base = get_engine_base(engine);
	struct mocs_table table;
	int local_fd;
	int i;

	local_fd = fd;
	if (local_fd == -1)
		local_fd = drm_open_driver_master(DRIVER_INTEL);

	igt_assert(get_mocs_settings(local_fd, &table, false));

	for (i = 0; i < table.size; i++)
		igt_assert_eq_u32(get_mocs_register_value(local_fd,
							  engine_base, i),
				  table.table[i].control_value);

	if (local_fd != fd)
		close(local_fd);
}

static void test_mocs_l3cc_values(int fd)
{
	uint32_t reg_values[MAX_NUMBER_MOCS_REGISTERS/2];
	struct mocs_table table;
	int local_fd;
	int i;

	local_fd = fd;
	if (local_fd == -1)
		local_fd = drm_open_driver_master(DRIVER_INTEL);

	for (i = 0; i < MAX_NUMBER_MOCS_REGISTERS / 2; i++)
		reg_values[i] = intel_register_read(GEN9_LNCFCMOCS0 + (i * 4));

	igt_assert(get_mocs_settings(local_fd, &table, false));

	for (i = 0; i < table.size / 2; i++) {
		igt_assert_eq_u32((reg_values[i] & 0xffff),
				  table.table[i * 2].l3cc_value);
		igt_assert_eq_u32((reg_values[i] >> 16),
				  table.table[i * 2 + 1].l3cc_value);
	}

	if (table.size & 1)
		igt_assert_eq_u32((reg_values[i] & 0xffff),
				  table.table[i * 2].l3cc_value);

	if (local_fd != fd)
		close(local_fd);
}

#define MI_STORE_REGISTER_MEM_64_BIT_ADDR	((0x24 << 23) | 2)

static int create_read_batch(struct drm_i915_gem_relocation_entry *reloc,
			     uint32_t *batch,
			     uint32_t dst_handle,
			     uint32_t size,
			     uint32_t reg_base)
{
	unsigned int offset = 0;

	for (uint32_t index = 0; index < size; index++, offset += 4) {
		batch[offset]   = MI_STORE_REGISTER_MEM_64_BIT_ADDR;
		batch[offset+1] = reg_base + (index * sizeof(uint32_t));
		batch[offset+2] = index * sizeof(uint32_t);	/* reloc */
		batch[offset+3] = 0;

		reloc[index].offset = (offset + 2) * sizeof(uint32_t);
		reloc[index].delta = index * sizeof(uint32_t);
		reloc[index].target_handle = dst_handle;
		reloc[index].write_domain = I915_GEM_DOMAIN_RENDER;
		reloc[index].read_domains = I915_GEM_DOMAIN_RENDER;
	}

	batch[offset++] = MI_BATCH_BUFFER_END;
	batch[offset++] = 0;

	return offset * sizeof(uint32_t);
}

static void do_read_registers(int fd,
			      uint32_t ctx_id,
			      uint32_t dst_handle,
			      uint32_t reg_base,
			      uint32_t size,
			      uint32_t engine_id)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj[2];
	struct drm_i915_gem_relocation_entry reloc[size];
	uint32_t batch[size * 4 + 4];
	uint32_t handle = gem_create(fd, 4096);

	memset(reloc, 0, sizeof(reloc));
	memset(obj, 0, sizeof(obj));
	memset(&execbuf, 0, sizeof(execbuf));

	obj[0].handle = dst_handle;

	obj[1].handle = handle;
	obj[1].relocation_count = size;
	obj[1].relocs_ptr = (uintptr_t) reloc;

	execbuf.buffers_ptr = (uintptr_t)obj;
	execbuf.buffer_count = 2;
	execbuf.batch_len =
		create_read_batch(reloc, batch, dst_handle, size, reg_base);
	i915_execbuffer2_set_context_id(execbuf, ctx_id);
	execbuf.flags = I915_EXEC_SECURE | engine_id;

	gem_write(fd, handle, 0, batch, execbuf.batch_len);
	gem_execbuf(fd, &execbuf);
	gem_close(fd, handle);
}

#define LOCAL_MI_LOAD_REGISTER_IMM	(0x22 << 23)

static int create_write_batch(uint32_t *batch,
			      const uint32_t *values,
			      uint32_t size,
			      uint32_t reg_base)
{
	unsigned int i;
	unsigned int offset = 0;

	batch[offset++] = LOCAL_MI_LOAD_REGISTER_IMM | (size * 2 - 1);

	for (i = 0; i < size; i++) {
		batch[offset++] = reg_base + (i * 4);
		batch[offset++] = values[i];
	}

	batch[offset++] = MI_BATCH_BUFFER_END;

	return offset * sizeof(uint32_t);
}

static void write_registers(int fd,
			    uint32_t ctx_id,
			    uint32_t reg_base,
			    const uint32_t *values,
			    uint32_t size,
			    uint32_t engine_id)
{
	struct drm_i915_gem_exec_object2 obj;
	struct drm_i915_gem_execbuffer2 execbuf;
	uint32_t batch[size * 4 + 2];
	uint32_t handle = gem_create(fd, 4096);

	memset(&obj, 0, sizeof(obj));
	memset(&execbuf, 0, sizeof(execbuf));

	obj.handle = handle;

	execbuf.buffers_ptr = (uintptr_t)&obj;
	execbuf.buffer_count = 1;
	execbuf.batch_len = create_write_batch(batch, values, size, reg_base);
	i915_execbuffer2_set_context_id(execbuf, ctx_id);
	execbuf.flags = I915_EXEC_SECURE | engine_id;

	gem_write(fd, handle, 0, batch, execbuf.batch_len);
	gem_execbuf(fd, &execbuf);
	gem_close(fd, handle);
}

static void check_control_registers(int fd,
				    unsigned engine,
				    uint32_t ctx_id,
				    bool dirty)
{
	const uint32_t reg_base  = get_engine_base(engine);
	uint32_t dst_handle = gem_create(fd, 4096);
	uint32_t *read_regs;
	struct mocs_table table;

	igt_assert(get_mocs_settings(fd, &table, dirty));

	do_read_registers(fd,
			  ctx_id,
			  dst_handle,
			  reg_base,
			  table.size,
			  engine);

	read_regs = gem_mmap__cpu(fd, dst_handle, 0, 4096, PROT_READ);

	gem_set_domain(fd, dst_handle, I915_GEM_DOMAIN_CPU, 0);
	for (int index = 0; index < table.size; index++)
		igt_assert_eq_u32(read_regs[index],
				  table.table[index].control_value);

	munmap(read_regs, 4096);
	gem_close(fd, dst_handle);
}

static void check_l3cc_registers(int fd,
				 unsigned engine,
				 uint32_t ctx_id,
				 bool dirty)
{
	struct mocs_table table;
	uint32_t dst_handle = gem_create(fd, 4096);
	uint32_t *read_regs;
	int index;

	igt_assert(get_mocs_settings(fd, &table, dirty));

	do_read_registers(fd,
			  ctx_id,
			  dst_handle,
			  GEN9_LNCFCMOCS0,
			  (table.size + 1) / 2,
			  engine);

	read_regs = gem_mmap__cpu(fd, dst_handle, 0, 4096, PROT_READ);

	gem_set_domain(fd, dst_handle, I915_GEM_DOMAIN_CPU, 0);
	for (index = 0; index < table.size / 2; index++) {
		igt_assert_eq_u32(read_regs[index] & 0xffff,
				  table.table[index * 2].l3cc_value);
		igt_assert_eq_u32(read_regs[index] >> 16,
				  table.table[index * 2 + 1].l3cc_value);
	}

	if (table.size & 1)
		igt_assert_eq_u32(read_regs[index] & 0xffff,
				  table.table[index * 2].l3cc_value);

	munmap(read_regs, 4096);
	gem_close(fd, dst_handle);
}

static void test_context_mocs_values(int fd, unsigned engine)
{
	int local_fd;
	uint32_t ctx_id = 0;

	local_fd = fd;
	if (local_fd == -1)
		local_fd = drm_open_driver_master(DRIVER_INTEL);

	check_control_registers(local_fd, engine, ctx_id, false);
	check_l3cc_registers(local_fd, engine, ctx_id, false);

	if (engine == I915_EXEC_RENDER) {
		ctx_id = gem_context_create(local_fd);

		check_control_registers(local_fd, engine, ctx_id, false);
		check_l3cc_registers(local_fd, engine, ctx_id, false);

		gem_context_destroy(local_fd, ctx_id);
	}

	if (local_fd != fd)
		close(local_fd);
}

static bool local_has_ring(int fd, unsigned engine)
{
	bool has_ring;
	int local_fd;

	if (get_engine_base(engine) == 0)
		return false;

	if (fd == -1)
		local_fd = drm_open_driver_master(DRIVER_INTEL);
	else
		local_fd = fd;

	has_ring = gem_has_ring(local_fd, engine);
	if (local_fd != fd)
		close(local_fd);

	return has_ring;
}

static void test_mocs_values(int fd)
{
	const struct intel_execution_engine *e;

	for (e = intel_execution_engines; e->name; e++) {
		unsigned engine = e->exec_id | e->flags;

		if (!local_has_ring(fd, engine))
			continue;

		igt_debug("Testing %s\n", e->name);
		test_mocs_control_values(fd, engine);
		test_context_mocs_values(fd, engine);
	}

	test_mocs_l3cc_values(fd);
}

static void default_context_tests(unsigned mode)
{
	int fd = drm_open_driver_master(DRIVER_INTEL);

	igt_debug("Testing Non/Default Context Engines\n");
	test_mocs_values(fd);

	switch (mode) {
	case NONE:	break;
	case RESET:	igt_force_gpu_reset();	break;
	case SUSPEND:	igt_system_suspend_autoresume(); break;
	case HIBERNATE:	igt_system_hibernate_autoresume(); break;
	}

	test_mocs_values(fd);
	close(fd);

	igt_debug("Testing Pristine Defaults\n");
	test_mocs_values(-1);
}

static void default_dirty_tests(unsigned mode)
{
	const struct intel_execution_engine *e;
	int fd = drm_open_driver_master(DRIVER_INTEL);

	igt_debug("Testing Dirty Default Context Engines\n");
	test_mocs_values(fd);

	for (e = intel_execution_engines; e->name; e++) {
		unsigned engine = e->exec_id | e->flags;

		if (!local_has_ring(fd, engine))
			continue;

		write_registers(fd, 0,
				GEN9_GFX_MOCS_0,
				write_values, ARRAY_SIZE(write_values),
				engine);

		write_registers(fd, 0,
				GEN9_LNCFCMOCS0,
				write_values, ARRAY_SIZE(write_values),
				engine);
	}

	switch (mode) {
	case NONE:	break;
	case RESET:	igt_force_gpu_reset();	break;
	case SUSPEND:	igt_system_suspend_autoresume(); break;
	case HIBERNATE:	igt_system_hibernate_autoresume(); break;
	}

	close(fd);

	igt_debug("Testing Pristine after Dirty Defaults\n");
	test_mocs_values(-1);
}

static void context_save_restore_test(unsigned mode)
{
	int fd = drm_open_driver_master(DRIVER_INTEL);
	uint32_t ctx_id = gem_context_create(fd);

	igt_debug("Testing Save Restore\n");

	check_control_registers(fd, I915_EXEC_RENDER, ctx_id, false);
	check_l3cc_registers(fd, I915_EXEC_RENDER, ctx_id, false);

	switch (mode) {
	case NONE:	break;
	case RESET:	igt_force_gpu_reset();	break;
	case SUSPEND:	igt_system_suspend_autoresume(); break;
	case HIBERNATE:	igt_system_hibernate_autoresume(); break;
	}

	check_control_registers(fd, I915_EXEC_RENDER, ctx_id, false);
	check_l3cc_registers(fd, I915_EXEC_RENDER, ctx_id, false);

	close(fd);
}

static void context_dirty_test(unsigned mode)
{
	int fd = drm_open_driver_master(DRIVER_INTEL);
	uint32_t ctx_id = gem_context_create(fd);

	igt_debug("Testing Dirty Context\n");
	test_mocs_values(fd);

	check_control_registers(fd, I915_EXEC_RENDER, ctx_id, false);
	check_l3cc_registers(fd, I915_EXEC_RENDER, ctx_id, false);

	/* XXX !RCS as well */

	write_registers(fd,
			ctx_id,
			GEN9_GFX_MOCS_0,
			write_values,
			ARRAY_SIZE(write_values),
			I915_EXEC_RENDER);

	write_registers(fd,
			ctx_id,
			GEN9_LNCFCMOCS0,
			write_values,
			ARRAY_SIZE(write_values),
			I915_EXEC_RENDER);

	check_control_registers(fd, I915_EXEC_RENDER, ctx_id, true);
	check_l3cc_registers(fd, I915_EXEC_RENDER, ctx_id, true);

	switch (mode) {
	case NONE:	break;
	case RESET:	igt_force_gpu_reset();	break;
	case SUSPEND:	igt_system_suspend_autoresume(); break;
	case HIBERNATE:	igt_system_hibernate_autoresume(); break;
	}

	check_control_registers(fd, I915_EXEC_RENDER, ctx_id, true);
	check_l3cc_registers(fd, I915_EXEC_RENDER, ctx_id, true);

	close(fd);

	/* Check that unmodified contexts are pristine */
	igt_debug("Testing Prestine Context (after dirty)\n");
	test_mocs_values(-1);
}

static void run_tests(unsigned mode)
{
	default_context_tests(mode);
	default_dirty_tests(mode);
	context_save_restore_test(mode);
	context_dirty_test(mode);
}

static void test_requirements(void)
{
	int fd = drm_open_driver_master(DRIVER_INTEL);
	struct mocs_table table;

	gem_require_mocs_registers(fd);
	igt_require(get_mocs_settings(fd, &table, false));
	close(fd);
}

igt_main
{
	struct pci_device *pci_dev;

	igt_fixture {
		test_requirements();

		pci_dev = intel_get_pci_device();
		igt_require(pci_dev);
		intel_register_access_init(pci_dev, 0);
	}

	igt_subtest("mocs-settings")
		run_tests(NONE);

	igt_subtest("mocs-reset")
		run_tests(RESET);

	igt_subtest("mocs-suspend")
		run_tests(SUSPEND);

	igt_subtest("mocs-hibernate")
		run_tests(HIBERNATE);

	igt_fixture {
		intel_register_access_fini();
	}
}

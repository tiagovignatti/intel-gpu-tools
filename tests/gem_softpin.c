/*
 * Copyright © 2015 Intel Corporation
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
 * Authors:
 *    Vinay Belgaumkar <vinay.belgaumkar@intel.com>
 *    Thomas Daniel <thomas.daniel@intel.com>
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <malloc.h>
#include "drm.h"
#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "intel_chipset.h"
#include "intel_io.h"
#include "i915_drm.h"
#include <assert.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include "igt_kms.h"
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>

#define BO_SIZE 4096
#define MULTIPAGE_BO_SIZE 2 * BO_SIZE
#define STORE_BATCH_BUFFER_SIZE 4
#define EXEC_OBJECT_PINNED	(1<<4)
#define EXEC_OBJECT_SUPPORTS_48B_ADDRESS (1<<3)
#define SHARED_BUFFER_SIZE 4096

typedef struct drm_i915_gem_userptr i915_gem_userptr;

static uint32_t init_userptr(int fd, i915_gem_userptr *, void *ptr, uint64_t size);
static void *create_mem_buffer(uint64_t size);
static int gem_call_userptr_ioctl(int fd, i915_gem_userptr *userptr);
static void gem_pin_userptr_test(void);
static void gem_pin_bo_test(void);
static void gem_pin_invalid_vma_test(bool test_decouple_flags, bool test_canonical_offset);
static void gem_pin_overlap_test(void);
static void gem_pin_high_address_test(void);

#define NO_PPGTT 0
#define ALIASING_PPGTT 1
#define FULL_32_BIT_PPGTT 2
#define FULL_48_BIT_PPGTT 3
/* uses_full_ppgtt
 * Finds supported PPGTT details.
 * @fd DRM fd
 * @min can be
 * 0 - No PPGTT
 * 1 - Aliasing PPGTT
 * 2 - Full PPGTT (32b)
 * 3 - Full PPGTT (48b)
 * RETURNS true/false if min support is present
*/
static bool uses_full_ppgtt(int fd, int min)
{
	struct drm_i915_getparam gp;
	int val = 0;

	memset(&gp, 0, sizeof(gp));
	gp.param = 18; /* HAS_ALIASING_PPGTT */
	gp.value = &val;

	if (drmIoctl(fd, DRM_IOCTL_I915_GETPARAM, &gp))
		return 0;

	errno = 0;
	return val >= min;
}

/* has_softpin_support
 * Finds if softpin feature is supported
 * @fd DRM fd
*/
static bool has_softpin_support(int fd)
{
	struct drm_i915_getparam gp;
	int val = 0;

	memset(&gp, 0, sizeof(gp));
	gp.param = 37; /* I915_PARAM_HAS_EXEC_SOFTPIN */
	gp.value = &val;

	if (drmIoctl(fd, DRM_IOCTL_I915_GETPARAM, &gp))
		return 0;

	errno = 0;
	return (val == 1);
}

/* gem_call_userptr_ioctl
 * Helper to call ioctl - TODO: move to lib
 * @fd - drm fd
 * @userptr - pointer to initialised userptr
 * RETURNS status of ioctl call
*/
static int gem_call_userptr_ioctl(int fd, i915_gem_userptr *userptr)
{
	int ret;

	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_USERPTR, userptr);

	if (ret)
		ret = errno;

	return ret;
}

/* init_userptr
 * Helper that inits userptr an returns handle
 * @fd - drm fd
 * @userptr - pointer to empty userptr
 * @ptr - buffer to be shared
 * @size - size of buffer
 * @ro - read only flag
 * RETURNS handle to shared buffer
*/
static uint32_t init_userptr(int fd, i915_gem_userptr *userptr, void *ptr,
			     uint64_t size)
{
	int ret;

	memset((void*)userptr, 0, sizeof(i915_gem_userptr));

	userptr->user_ptr = (unsigned long)ptr; /* Need the cast to overcome compiler warning */
	userptr->user_size = size;
	userptr->flags = 0; /* use synchronized operation */

	ret = gem_call_userptr_ioctl(fd, userptr);
	igt_assert_eq(ret, 0);

	return userptr->handle;
}

/* create_mem_buffer
 * Creates a 4K aligned CPU buffer
 * @size - size of buffer
 * RETURNS pointer to buffer of @size
*/
static void *create_mem_buffer(uint64_t size)
{
	void *addr;
	int ret;

	ret = posix_memalign(&addr, 4096, size);
	igt_assert(ret == 0);

	return addr;
}

/* setup_exec_obj
 * populate exec object
 * @exec - exec object
 * @handle - handle to gem buffer
 * @flags - any flags
 * @offset - requested VMA
*/
static void setup_exec_obj(struct drm_i915_gem_exec_object2 *exec,
			   uint32_t handle, uint32_t flags,
			   uint64_t offset)
{
	memset(exec, 0, sizeof(struct drm_i915_gem_exec_object2));
	exec->handle = handle;
	exec->flags = flags;
	exec->offset = offset;
}

/* gen8_canonical_addr
 * Used to convert any address into canonical form, i.e. [63:48] == [47].
 * Based on kernel's sign_extend64 implementation.
 * @address - a virtual address
*/
#define GEN8_HIGH_ADDRESS_BIT 47
static uint64_t gen8_canonical_addr(uint64_t address)
{
	__u8 shift = 63 - GEN8_HIGH_ADDRESS_BIT;
	return (__s64)(address << shift) >> shift;
}

/* gem_store_data_svm
 * populate batch buffer with MI_STORE_DWORD_IMM command
 * @fd: drm file descriptor
 * @cmd_buf: batch buffer
 * @vaddr: destination Virtual address
 * @data: data to be store at destination
 * @end: whether to end batch buffer or not
*/
static int gem_store_data_svm(int fd, uint32_t *cmd_buf, uint64_t vaddr,
			      uint32_t data, bool end)
{
	int i = 0;

	cmd_buf[i++] = MI_STORE_DWORD_IMM;
	cmd_buf[i++] = vaddr & 0xFFFFFFFC;
	cmd_buf[i++] = (vaddr >> 32) & 0xFFFF; /* bits 32:47 */

	cmd_buf[i++] = data;
	if (end) {
		cmd_buf[i++] = MI_BATCH_BUFFER_END;
		cmd_buf[i++] = 0;
	}

	return(i * sizeof(uint32_t));
}

/* gem_store_data
 * populate batch buffer with MI_STORE_DWORD_IMM command
 * This one fills up reloc buffer as well
 * @fd: drm file descriptor
 * @cmd_buf: batch buffer
 * @data: data to be store at destination
 * @reloc - relocation entry
 * @end: whether to end batch buffer or not
*/
static int gem_store_data(int fd, uint32_t *cmd_buf,
			  uint32_t handle, uint32_t data,
			  struct drm_i915_gem_relocation_entry *reloc,
			  bool end)
{
	int i = 0;

	cmd_buf[i++] = MI_STORE_DWORD_IMM;
	cmd_buf[i++] = 0; /* lower 31 bits of 48 bit address - 0 reloc needed */
	cmd_buf[i++] = 0; /* upper 15 bits of 48 bit address - 0 reloc needed */
	reloc->offset = 1 * sizeof(uint32_t);
	reloc->delta = 0;
	reloc->target_handle = handle;
	reloc->read_domains = I915_GEM_DOMAIN_RENDER;
	reloc->write_domain = I915_GEM_DOMAIN_RENDER;
	reloc->presumed_offset = 0;
	cmd_buf[i++] = data;
	if (end) {
		cmd_buf[i++] = MI_BATCH_BUFFER_END;
		cmd_buf[i++] = 0;
	}

	return (i * sizeof(uint32_t));
}

/* setup_execbuffer
 * helper for buffer execution
 * @execbuf - pointer to execbuffer
 * @exec_object - pointer to exec object2 struct
 * @ring - ring to be used
 * @buffer_count - how manu buffers to submit
 * @batch_length - length of batch buffer
*/
static void setup_execbuffer(struct drm_i915_gem_execbuffer2 *execbuf,
			     struct drm_i915_gem_exec_object2 *exec_object,
			     int ring, int buffer_count, int batch_length)
{
	execbuf->buffers_ptr = (unsigned long)exec_object;
	execbuf->buffer_count = buffer_count;
	execbuf->batch_start_offset = 0;
	execbuf->batch_len = batch_length;
	execbuf->cliprects_ptr = 0;
	execbuf->num_cliprects = 0;
	execbuf->DR1 = 0;
	execbuf->DR4 = 0;
	execbuf->flags = ring;
	i915_execbuffer2_set_context_id(*execbuf, 0);
	execbuf->rsvd2 = 0;
}

/* submit_and_sync
 * Helper function for exec and sync functions
 * @fd - drm fd
 * @execbuf - pointer to execbuffer
 * @batch_buf_handle - batch buffer handle
*/
static void submit_and_sync(int fd, struct drm_i915_gem_execbuffer2 *execbuf,
			    uint32_t batch_buf_handle)
{
	gem_execbuf(fd, execbuf);
	gem_sync(fd, batch_buf_handle);
}

/* gem_userptr_sync
 * helper for syncing to CPU domain - copy/paste from userblit
 * @fd - drm fd
 * @handle - buffer handle to sync
*/
static void gem_userptr_sync(int fd, uint32_t handle)
{
	gem_set_domain(fd, handle, I915_GEM_DOMAIN_CPU, I915_GEM_DOMAIN_CPU);
}


/* gem_pin_userptr_test
 * This test will create a shared buffer, and create a command
 * for GPU to write data in it
 * CPU will read and make sure expected value is obtained
 * Malloc a 4K buffer
 * Share buffer with with GPU by using userptr ioctl
 * Create batch buffer to write DATA to first dword of buffer
 * Use 0x1000 address as destination address in batch buffer
 * Set EXEC_OBJECT_PINNED flag in exec object
 * Set 'offset' in exec object to 0x1000
 * Submit execbuffer
 * Verify value of first DWORD in shared buffer matches DATA
*/
static void gem_pin_userptr_test(void)
{
	i915_gem_userptr userptr;
	int fd;
	uint32_t *shared_buffer;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec_object2[2];
	uint32_t batch_buffer[STORE_BATCH_BUFFER_SIZE + 2];
	uint32_t batch_buf_handle, shared_buf_handle;
	int ring, len;
	const uint32_t data = 0x12345678;
	uint64_t pinning_offset = 0x1000;

	fd = drm_open_driver(DRIVER_INTEL);
	igt_require(uses_full_ppgtt(fd, ALIASING_PPGTT));
	igt_require(has_softpin_support(fd));
	batch_buf_handle = gem_create(fd, BO_SIZE);

	/* create cpu buffer */
	shared_buffer = create_mem_buffer(BO_SIZE);

	/* share with GPU */
	shared_buf_handle = init_userptr(fd, &userptr, shared_buffer,
					 BO_SIZE);

	/* create command buffer with write command */
	len = gem_store_data_svm(fd, batch_buffer, pinning_offset, data, true);
	gem_write(fd, batch_buf_handle, 0, batch_buffer, len);

	/* submit command buffer */
	setup_exec_obj(&exec_object2[0], shared_buf_handle,
		       EXEC_OBJECT_PINNED, pinning_offset);
	setup_exec_obj(&exec_object2[1], batch_buf_handle, 0, 0);

	ring = I915_EXEC_RENDER;

	setup_execbuffer(&execbuf, exec_object2, ring, 2, len);
	submit_and_sync(fd, &execbuf, batch_buf_handle);
	gem_userptr_sync(fd, shared_buf_handle);

	/* Check if driver pinned the buffer as requested */
	igt_fail_on_f(exec_object2[0].offset != pinning_offset,
			"\nFailed to pin at requested offset");
	/* check on CPU to see if value changes */
	igt_fail_on_f(shared_buffer[0] != data,
		      "\nCPU read does not match GPU write,\
			expected: 0x%x, got: 0x%x\n",
			data, shared_buffer[0]);

	gem_close(fd, batch_buf_handle);
	gem_close(fd, shared_buf_handle);
	close(fd);
	free(shared_buffer);
}

/* gem_pin_bo
 * This test will test softpinning of a gem buffer object
 * Malloc a 4K buffer
 * Create batch buffer to write DATA to first dword of buffer
 * Use 0x1000 address as destination address in batch buffer
 * Set EXEC_OBJECT_PINNED flag in exec object
 * Set 'offset' in exec object to 0x1000
 * Submit execbuffer
 * Verify value pinned offset matches the request
*/
static void gem_pin_bo_test(void)
{
	int fd;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec_object2[2];
	uint32_t batch_buffer[STORE_BATCH_BUFFER_SIZE + 2];
	uint32_t batch_buf_handle, unshared_buf_handle;
	struct drm_i915_gem_relocation_entry reloc[4];
	int ring, len;
	uint32_t value;
	const uint32_t data = 0x12345678;
	uint64_t pinning_offset = 0x1000;

	fd = drm_open_driver(DRIVER_INTEL);
	igt_require(uses_full_ppgtt(fd, ALIASING_PPGTT));
	igt_require(has_softpin_support(fd));

	batch_buf_handle = gem_create(fd, BO_SIZE);

	/* create gem buffer */
	unshared_buf_handle = gem_create(fd, BO_SIZE);

	/* create command buffer with write command */
	len = gem_store_data(fd, batch_buffer, unshared_buf_handle, data,
				reloc, true);
	gem_write(fd, batch_buf_handle, 0, batch_buffer, len);

	/* submit command buffer */
	setup_exec_obj(&exec_object2[0], unshared_buf_handle,
		       EXEC_OBJECT_PINNED, pinning_offset);
	setup_exec_obj(&exec_object2[1], batch_buf_handle, 0, 0);
	exec_object2[1].relocation_count = 1;
	exec_object2[1].relocs_ptr = (unsigned long)reloc;

	ring = I915_EXEC_RENDER;

	setup_execbuffer(&execbuf, exec_object2, ring, 2, len);
	submit_and_sync(fd, &execbuf, batch_buf_handle);

	/* Check if driver pinned the buffer as requested */
	igt_fail_on_f(exec_object2[0].offset != pinning_offset,
			"\nFailed to pin at requested offset");
	gem_read(fd, unshared_buf_handle, 0, (void*)&value, 4);
	igt_assert(value == data);

	gem_close(fd, batch_buf_handle);
	gem_close(fd, unshared_buf_handle);
	close(fd);
}


/* gem_multiple_process_test
 * Run basic test simultaneously with multiple processes
 * This will test pinning same VA separately in each process

 * fork();
 * Execute basic test in parent/child processes
*/
#define MAX_NUM_PROCESSES 10

static void gem_multiple_process_test(void)
{
	int fd;

	fd = drm_open_driver(DRIVER_INTEL);
	igt_require(uses_full_ppgtt(fd, ALIASING_PPGTT));
	igt_require(has_softpin_support(fd));

	igt_fork(child, MAX_NUM_PROCESSES) {
		gem_pin_userptr_test();
	}
	igt_waitchildren();

	close(fd);
}


/* gem_repin_test
 * This test tries to repin a buffer at a previously pinned vma
 * from a different execbuf.
 * Malloc a 4K buffer
 * Share buffer with with GPU by using userptr ioctl
 * Create batch buffer to write DATA to first dword of buffer
 * Use 0x1000 address as destination address in batch buffer
 * Set EXEC_OBJECT_PINNED flag in exec object
 * Set 'offset' in exec object to 0x1000 VMA
 * Submit execbuffer
 * Verify value of first DWORD in shared buffer matches DATA

 * Create second shared buffer
 * Follow all steps above
 * Execpt, for offset, use VMA of first buffer above
 * Submit execbuffer
 * Verify value of first DWORD in second shared buffer matches DATA
*/
static void gem_repin_test(void)
{
	i915_gem_userptr userptr;
	i915_gem_userptr userptr1;
	int fd;
	uint32_t *shared_buffer;
	uint32_t *shared_buffer1;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec_object2[2];
	uint32_t batch_buffer[STORE_BATCH_BUFFER_SIZE + 2];
	uint32_t batch_buf_handle, shared_buf_handle, shared_buf_handle1;
	int ring, len;
	const uint32_t data = 0x12345678;
	uint64_t pinning_offset = 0x1000;

	/* Create gem object */
	fd = drm_open_driver(DRIVER_INTEL);
	igt_require(uses_full_ppgtt(fd, ALIASING_PPGTT));
	igt_require(has_softpin_support(fd));

	batch_buf_handle = gem_create(fd, BO_SIZE);

	/* create cpu buffer, set first elements to 0x0 */
	shared_buffer = create_mem_buffer(BO_SIZE);
	shared_buffer1 = create_mem_buffer(BO_SIZE);
	shared_buffer[0] = 0x0;
	shared_buffer1[0] = 0x0;

	/* share with GPU and get handles */
	shared_buf_handle = init_userptr(fd, &userptr, shared_buffer,
					 BO_SIZE);
	shared_buf_handle1 = init_userptr(fd, &userptr1, shared_buffer1,
					  BO_SIZE);

	/* create command buffer with write command */
	len = gem_store_data_svm(fd, batch_buffer, pinning_offset, data, true);
	gem_write(fd, batch_buf_handle, 0, batch_buffer, len);

	/* submit command buffer */
	setup_exec_obj(&exec_object2[0], shared_buf_handle,
		       EXEC_OBJECT_PINNED, pinning_offset);
	setup_exec_obj(&exec_object2[1], batch_buf_handle, 0, 0);

	ring = I915_EXEC_RENDER;

	setup_execbuffer(&execbuf, exec_object2, ring, 2, len);
	submit_and_sync(fd, &execbuf, batch_buf_handle);
	gem_userptr_sync(fd, shared_buf_handle);

	igt_assert(exec_object2[0].offset == pinning_offset);
	igt_assert(*shared_buffer == data);

	/* Second buffer */
	/* create command buffer with write command */
	pinning_offset = exec_object2[0].offset;
	len = gem_store_data_svm(fd, batch_buffer, pinning_offset, data, true);
	gem_write(fd, batch_buf_handle, 0, batch_buffer, len);

	/* submit command buffer */
	/* Pin at shared_buffer, not shared_buffer1 */
	/* We are requesting address where another buffer was pinned previously */
	setup_exec_obj(&exec_object2[0], shared_buf_handle1,
		       EXEC_OBJECT_PINNED, pinning_offset);
	setup_exec_obj(&exec_object2[1], batch_buf_handle, 0, 0);

	ring = I915_EXEC_RENDER;

	setup_execbuffer(&execbuf, exec_object2, ring, 2, len);
	submit_and_sync(fd, &execbuf, batch_buf_handle);
	gem_userptr_sync(fd, shared_buf_handle1);

	igt_assert(exec_object2[0].offset == pinning_offset);
	igt_assert(*shared_buffer1 == data);

	gem_close(fd, batch_buf_handle);
	gem_close(fd, shared_buf_handle);
	close(fd);

	free(shared_buffer);
	free(shared_buffer1);
}


/* gem_repin_overlap_test
 * This test will attempt to pin two buffers at the same VMA as part of the same
   execbuffer object

 * Malloc a 4K buffer
 * Share buffer with with GPU by using userptr ioctl
 * Create second shared buffer
 * Create batch buffer to write DATA to first dword of each buffer
 * Use same virtual address as destination addresses in batch buffer
 * Set EXEC_OBJECT_PINNED flag in both exec objects
 * Set 'offset' in both exec objects to same VMA
 * Submit execbuffer
 * Command should return EINVAL, since we are trying to pin to same VMA
*/
static void gem_pin_overlap_test(void)
{
	i915_gem_userptr userptr;
	i915_gem_userptr userptr1;
	int fd, ret;
	uint32_t *shared_buffer;
	uint32_t *shared_buffer1;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec_object2[3];
	uint32_t shared_buf_handle, shared_buf_handle1;
	int ring, len;
	uint64_t pinning_offset = 0x1000;

	fd = drm_open_driver(DRIVER_INTEL);
	igt_require(uses_full_ppgtt(fd, ALIASING_PPGTT));
	igt_require(has_softpin_support(fd));

	shared_buffer = create_mem_buffer(BO_SIZE);
	shared_buffer1 = create_mem_buffer(BO_SIZE * 2);

	/* share with GPU */
	shared_buf_handle = init_userptr(fd, &userptr, shared_buffer,
					 BO_SIZE);
	shared_buf_handle1 = init_userptr(fd, &userptr1, shared_buffer1,
					  BO_SIZE * 2);

	/* submit command buffer */
	setup_exec_obj(&exec_object2[0], shared_buf_handle,
		       EXEC_OBJECT_PINNED, pinning_offset);
	setup_exec_obj(&exec_object2[1], shared_buf_handle1,
		       EXEC_OBJECT_PINNED, pinning_offset);

	ring = I915_EXEC_RENDER;

	setup_execbuffer(&execbuf, exec_object2, ring, 2, len);

	ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_EXECBUFFER2, &execbuf);

	/* expect to fail */
	igt_assert_neq(ret, 0);
	igt_assert(errno == EINVAL);

	close(fd);
	free(shared_buffer);
	free(shared_buffer1);
}

/* gem_softpin_stress_test
 * Stress test which creates 10K buffers and shares with GPU
 * Create 100K uint32 buffers of size 4K each
 * Share with GPU using userptr ioctl
 * Create batch buffer to write DATA in first element of each buffer
 * Pin each buffer to varying addresses starting from 0x800000000000 going below
 * (requires offsets in canonical form)
 * Execute Batch Buffer on Blit ring STRESS_NUM_LOOPS times
 * Validate every buffer has DATA in first element
 * Rinse and Repeat on Render ring
*/
#define STRESS_NUM_BUFFERS 100000
#define STRESS_NUM_LOOPS 100
#define STRESS_STORE_COMMANDS 4 * STRESS_NUM_BUFFERS
#define STRESS_START_ADDRESS 0x800000000000
static void gem_softpin_stress_test(void)
{
	i915_gem_userptr userptr;
	int fd;
	uint32_t **shared_buffer;
	uint32_t *shared_handle;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 *exec_object2;
	uint32_t *batch_buffer;
	uint32_t batch_buf_handle;
	int ring, len;
	int buf, loop;
	uint64_t pinning_offset = STRESS_START_ADDRESS;

	fd = drm_open_driver(DRIVER_INTEL);
	igt_require(uses_full_ppgtt(fd, FULL_48_BIT_PPGTT));
	igt_require(has_softpin_support(fd));


	/* Allocate blobs for all data structures */
	shared_handle = calloc(STRESS_NUM_BUFFERS, sizeof(uint32_t));
	shared_buffer = calloc(STRESS_NUM_BUFFERS, sizeof(uint32_t *));
	exec_object2 = calloc(STRESS_NUM_BUFFERS + 1,
				sizeof(struct drm_i915_gem_exec_object2));
	/* 4 dwords per buffer + 2 for the end of batchbuffer */
	batch_buffer = calloc(STRESS_STORE_COMMANDS + 2, sizeof(uint32_t));
	batch_buf_handle = gem_create(fd, (STRESS_STORE_COMMANDS + 2)*4);

	/* create command buffer with write commands */
	len = 0;
	for(buf = 0; buf < STRESS_NUM_BUFFERS; buf++) {
		shared_buffer[buf] = create_mem_buffer(BO_SIZE);
		*shared_buffer[buf] = 0xFFFFFFFF;

		/* share with GPU */
		shared_handle[buf] = init_userptr(fd, &userptr,
						  shared_buffer[buf],
						  BO_SIZE);

		setup_exec_obj(&exec_object2[buf], shared_handle[buf],
			       EXEC_OBJECT_PINNED |
			       EXEC_OBJECT_SUPPORTS_48B_ADDRESS,
			       gen8_canonical_addr(pinning_offset));
		len += gem_store_data_svm(fd, batch_buffer + (len/4),
					  gen8_canonical_addr(pinning_offset),
					  buf, (buf == STRESS_NUM_BUFFERS-1)? \
					  true:false);

		/* decremental 4K aligned address */
		pinning_offset -= ALIGN(BO_SIZE, 4096);
	}

	/* setup command buffer */
	gem_write(fd, batch_buf_handle, 0, batch_buffer, len);
	setup_exec_obj(&exec_object2[STRESS_NUM_BUFFERS], batch_buf_handle,
		       0, 0);

	/* We want to run this on BLT ring if possible */
	if (HAS_BLT_RING(intel_get_drm_devid(fd))) {
		ring = I915_EXEC_BLT;

		setup_execbuffer(&execbuf, exec_object2, ring,
				 STRESS_NUM_BUFFERS + 1, len);

		for (loop = 0; loop < STRESS_NUM_LOOPS; loop++) {
			submit_and_sync(fd, &execbuf, batch_buf_handle);
			/* Set pinning offset back to original value */
			pinning_offset = STRESS_START_ADDRESS;
			for(buf = 0; buf < STRESS_NUM_BUFFERS; buf++) {
				gem_userptr_sync(fd, shared_handle[buf]);
				igt_assert(exec_object2[buf].offset ==
					gen8_canonical_addr(pinning_offset));
				igt_fail_on_f(*shared_buffer[buf] != buf, \
				"Mismatch in buffer %d, iteration %d: 0x%08X\n", \
				buf, loop, *shared_buffer[buf]);
				pinning_offset -= ALIGN(BO_SIZE, 4096);
			}
			/* Reset the buffer entries for next iteration */
			for(buf = 0; buf < STRESS_NUM_BUFFERS; buf++) {
				*shared_buffer[buf] = 0xFFFFFFFF;
			}
		}
	}

	/* Now Render Ring */
	ring = I915_EXEC_RENDER;
	setup_execbuffer(&execbuf, exec_object2, ring,
			 STRESS_NUM_BUFFERS + 1, len);
	for (loop = 0; loop < STRESS_NUM_LOOPS; loop++) {
		submit_and_sync(fd, &execbuf, batch_buf_handle);
		pinning_offset = STRESS_START_ADDRESS;
		for(buf = 0; buf < STRESS_NUM_BUFFERS; buf++) {
			gem_userptr_sync(fd, shared_handle[buf]);
			igt_assert(exec_object2[buf].offset ==
				gen8_canonical_addr(pinning_offset));
			igt_fail_on_f(*shared_buffer[buf] != buf, \
			"Mismatch in buffer %d, \
			iteration %d: 0x%08X\n", buf, loop, *shared_buffer[buf]);
			pinning_offset -= ALIGN(BO_SIZE, 4096);
		}
		/* Reset the buffer entries for next iteration */
		for(buf = 0; buf < STRESS_NUM_BUFFERS; buf++) {
			*shared_buffer[buf] = 0xFFFFFFFF;
		}
	}

	for(buf = 0; buf < STRESS_NUM_BUFFERS; buf++) {
		gem_close(fd, shared_handle[buf]);
		free(shared_buffer[buf]);
	}
	gem_close(fd, batch_buf_handle);
	close(fd);

	free(shared_handle);
	free(shared_buffer);
	free(exec_object2);
	free(batch_buffer);
}

/* gem_write_multipage_buffer
 * Create a buffer spanning multiple pages, and share with GPU.
 * Write to every element of the buffer
 * and verify correct contents.

 * Create 8K buffer
 * Share with GPU using userptr ioctl
 * Create batch buffer to write DATA in all elements of buffer
 * Execute Batch Buffer
 * Validate every element has DATA
*/

#define DWORD_SIZE sizeof(uint32_t)
#define BB_SIZE ((MULTIPAGE_BO_SIZE / DWORD_SIZE) * STORE_BATCH_BUFFER_SIZE) + 2
#define NUM_DWORDS (MULTIPAGE_BO_SIZE/sizeof(uint32_t))
static void gem_write_multipage_buffer_test(void)
{
	i915_gem_userptr userptr;
	int fd;
	uint32_t *shared_buffer;
	uint32_t shared_handle;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec_object2[2];
	uint32_t batch_buffer[BB_SIZE];
	uint32_t batch_buf_handle;
	int ring, len, j;
	uint64_t pinning_offset=0x1000;
	uint64_t vaddr;

	fd = drm_open_driver(DRIVER_INTEL);
	igt_require(uses_full_ppgtt(fd, ALIASING_PPGTT));
	igt_require(has_softpin_support(fd));

	batch_buf_handle = gem_create(fd, sizeof(batch_buffer));
	shared_buffer = create_mem_buffer(MULTIPAGE_BO_SIZE);

	len = 0;
	memset(batch_buffer, 0, sizeof(batch_buffer));
	memset(shared_buffer, 0, MULTIPAGE_BO_SIZE);

	/* share with GPU */
	shared_handle = init_userptr(fd, &userptr, shared_buffer,
				     MULTIPAGE_BO_SIZE);
	setup_exec_obj(&exec_object2[0], shared_handle,
		       EXEC_OBJECT_PINNED, pinning_offset);

	/* create command buffer with write commands */
	vaddr = pinning_offset;
	for(j=0; j< NUM_DWORDS; j++) {
		len += gem_store_data_svm(fd, batch_buffer + (len/4), vaddr,
					  j,
					  (j == NUM_DWORDS - 1) ? true:false);
		vaddr += sizeof(shared_buffer[0]);  /* 4 bytes */
	}

	gem_write(fd, batch_buf_handle, 0, batch_buffer, len);

	/* submit command buffer */
	setup_exec_obj(&exec_object2[1], batch_buf_handle, 0, 0);

	ring = I915_EXEC_RENDER;
	setup_execbuffer(&execbuf, exec_object2, ring, 2, len);
	submit_and_sync(fd, &execbuf, batch_buf_handle);
	gem_userptr_sync(fd, shared_handle);

	igt_assert(exec_object2[0].offset == pinning_offset);
	for(j = 0; j < (MULTIPAGE_BO_SIZE/sizeof(uint32_t)); j++) {
		igt_fail_on_f(shared_buffer[j] != j,
		"Mismatch in index %d: 0x%08X\n", j, shared_buffer[j]);
	}

	gem_close(fd, batch_buf_handle);
	gem_close(fd, shared_handle);
	close(fd);

	free(shared_buffer);
}

/* gem_pin_invalid_vma_test
 * This test will request to pin a shared buffer to an invalid
 * VMA  > 48-bit address if system supports 48B PPGTT; it also
 * will test that any attempt of using a 48-bit address requires
 * the SUPPORTS_48B_ADDRESS flag, and that 48-bit address need to be
 * in canonical form (bits [63:48] == [47]).
 * If system supports 32B PPGTT, it will test the equivalent invalid VMA
 * Create shared buffer of size 4K
 * Try and Pin object to invalid address
*/
static void gem_pin_invalid_vma_test(bool test_decouple_flags,
				     bool test_canonical_offset)
{
	i915_gem_userptr userptr;
	int fd, ret;
	uint32_t *shared_buffer;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec_object2[1];
	uint32_t shared_buf_handle;
	int ring;
	uint64_t invalid_address_for_48b = 0x9000000000000; /* 52 bit address */
	uint64_t noncanonical_address_for_48b = 0xFF0000000000; /* 48 bit address in noncanonical form */
	uint64_t invalid_address_for_32b = 0x900000000; /* 36 bit address */

	fd = drm_open_driver(DRIVER_INTEL);
	igt_require(uses_full_ppgtt(fd, FULL_48_BIT_PPGTT) ||
		    uses_full_ppgtt(fd, FULL_32_BIT_PPGTT));
	igt_require(has_softpin_support(fd));

	shared_buffer = create_mem_buffer(BO_SIZE);
	*shared_buffer = 0xFFFFFFFF;

	/* share with GPU */
	shared_buf_handle = init_userptr(fd, &userptr, shared_buffer, BO_SIZE);

	if (uses_full_ppgtt(fd, FULL_48_BIT_PPGTT) && test_canonical_offset) {
		setup_exec_obj(&exec_object2[0], shared_buf_handle,
			       EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS,
			       noncanonical_address_for_48b);
	} else if (uses_full_ppgtt(fd, FULL_48_BIT_PPGTT) && !test_decouple_flags) {
		setup_exec_obj(&exec_object2[0], shared_buf_handle,
			       EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS,
			       invalid_address_for_48b);
	} else {
		/* This also fails in 48b without 48B_ADDRESS support flag */
		setup_exec_obj(&exec_object2[0], shared_buf_handle,
			       EXEC_OBJECT_PINNED, invalid_address_for_32b);
	}

	ring = I915_EXEC_RENDER;

	setup_execbuffer(&execbuf, exec_object2, ring, 1, 0);

	/* Expect execbuf to fail */
	ret = drmIoctl(fd,
		       DRM_IOCTL_I915_GEM_EXECBUFFER2,
		       &execbuf);

	igt_assert(errno == EINVAL);
	igt_assert_neq(ret, 0);

	gem_close(fd, shared_buf_handle);
	close(fd);
	free(shared_buffer);
}


/* gem_pin_high_address_test
 * This test will create a shared buffer, and create a command
 * for GPU to write data in it. It will attempt to pin the buffer at address > 32 bits.
 * CPU will read and make sure expected value is obtained

 * Malloc a 4K buffer
 * Share buffer with with GPU by using userptr ioctl
 * Create batch buffer to write DATA to first dword of buffer
 * Use virtual address of buffer as 0x1100000000 (> 32 bit)
 * Set EXEC_OBJECT_PINNED flag in exec object
 * Set 'offset' in exec object to shared buffer VMA
 * Submit execbuffer
 * Verify value of first DWORD in shared buffer matches DATA
*/

static void gem_pin_high_address_test(void)
{
	i915_gem_userptr userptr;
	int fd;
	uint32_t *shared_buffer;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec_object2[2];
	uint32_t batch_buffer[STORE_BATCH_BUFFER_SIZE + 2];
	uint32_t batch_buf_handle, shared_buf_handle;
	int ring, len;
	const uint32_t data = 0x12345678;
	uint64_t high_address = 0x1111FFFF000; /* 44 bit address */

	fd = drm_open_driver(DRIVER_INTEL);
	igt_require(uses_full_ppgtt(fd, FULL_48_BIT_PPGTT));
	igt_require(has_softpin_support(fd));

	batch_buf_handle = gem_create(fd, BO_SIZE);

	/* create cpu buffer, set to all 0xF's */
	shared_buffer = create_mem_buffer(BO_SIZE);
	*shared_buffer = 0xFFFFFFFF;

	/* share with GPU */
	shared_buf_handle = init_userptr(fd, &userptr, shared_buffer, BO_SIZE);

	/* create command buffer with write command */
	len = gem_store_data_svm(fd, batch_buffer, high_address, data, true);
	gem_write(fd, batch_buf_handle, 0, batch_buffer, len);

	/* submit command buffer */
	setup_exec_obj(&exec_object2[0], shared_buf_handle,
		       EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS, high_address);
	setup_exec_obj(&exec_object2[1], batch_buf_handle, 0, 0);

	ring = I915_EXEC_RENDER;

	setup_execbuffer(&execbuf, exec_object2, ring, 2, len);
	submit_and_sync(fd, &execbuf, batch_buf_handle);
	gem_userptr_sync(fd, shared_buf_handle);

	igt_assert(exec_object2[0].offset == high_address);
	/* check on CPU to see if value changes */
	igt_fail_on_f(shared_buffer[0] != data,
		"\nCPU read does not match GPU write, \
		expected: 0x%x, got: 0x%x\n", data, shared_buffer[0]);

	gem_close(fd, batch_buf_handle);
	gem_close(fd, shared_buf_handle);
	close(fd);
	free(shared_buffer);
}

/* gem_pin_near_48Bit_test
 * This test will create a shared buffer,
 * and create a command for GPU to write data in it. It will attempt
 * to pin the buffer at address > 47 bits <= 48-bit.
 * CPU will read and make sure expected value is obtained.
 * Note that we must submit addresses in canonical form, not only
 * because the addresss will be validated, but also the returned offset
 * will be in this format.

 * Malloc a 4K buffer
 * Share buffer with with GPU by using userptr ioctl
 * Create batch buffer to write DATA to first dword of buffer
 * Use virtual address of buffer as range between 47-bit and 48-bit
 * Set EXEC_OBJECT_PINNED flag in exec object
 * Set 'offset' in exec object to shared buffer VMA
 * Submit execbuffer
 * Verify value of first DWORD in shared buffer matches DATA
*/
#define BEGIN_HIGH_ADDRESS 0x7FFFFFFFF000
#define END_HIGH_ADDRESS 0xFFFFFFFFC000
#define ADDRESS_INCREMENT 0x2000000000
static void gem_pin_near_48Bit_test(void)
{
	i915_gem_userptr userptr;
	int fd;
	uint32_t *shared_buffer;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec_object2[2];
	uint32_t batch_buffer[BO_SIZE];
	uint32_t batch_buf_handle, shared_buf_handle;
	int ring, len;
	const uint32_t data = 0x12345678;
	uint64_t high_address, can_high_address;

	fd = drm_open_driver(DRIVER_INTEL);
	igt_require(uses_full_ppgtt(fd, FULL_48_BIT_PPGTT));
	igt_require(has_softpin_support(fd));

	batch_buf_handle = gem_create(fd, BO_SIZE);

	/* create cpu buffer, set to all 0xF's */
	shared_buffer = create_mem_buffer(BO_SIZE);
	*shared_buffer = 0xFFFFFFFF;

	/* share with GPU */
	shared_buf_handle = init_userptr(fd, &userptr, shared_buffer, BO_SIZE);

	for (high_address = BEGIN_HIGH_ADDRESS; high_address <= END_HIGH_ADDRESS;
						high_address+=ADDRESS_INCREMENT) {
		can_high_address = gen8_canonical_addr(high_address);
		/* create command buffer with write command */
		len = gem_store_data_svm(fd, batch_buffer, can_high_address,
					data, true);
		gem_write(fd, batch_buf_handle, 0, batch_buffer, len);
		/* submit command buffer */
		setup_exec_obj(&exec_object2[0], shared_buf_handle,
			       EXEC_OBJECT_PINNED | EXEC_OBJECT_SUPPORTS_48B_ADDRESS,
			       can_high_address);
		setup_exec_obj(&exec_object2[1], batch_buf_handle, 0, 0);

		ring = I915_EXEC_RENDER;
		setup_execbuffer(&execbuf, exec_object2, ring, 2, len);
		submit_and_sync(fd, &execbuf, batch_buf_handle);
		gem_userptr_sync(fd, shared_buf_handle);

		igt_assert(exec_object2[0].offset == can_high_address);
		/* check on CPU to see if value changes */
		igt_fail_on_f(shared_buffer[0] != data,
		"\nCPU read does not match GPU write, expected: 0x%x, \
		got: 0x%x\n, 0x%"PRIx64"", data, shared_buffer[0], high_address);
	}

	gem_close(fd, batch_buf_handle);
	gem_close(fd, shared_buf_handle);
	close(fd);
	free(shared_buffer);
}


int main(int argc, char* argv[])
{
	igt_subtest_init(argc, argv);
	igt_skip_on_simulation();

	/* All tests need PPGTT support */
	igt_subtest("gem_pin_userptr") {
		gem_pin_userptr_test();
	}
	igt_subtest("gem_pin_bo") {
		gem_pin_bo_test();
	}
	igt_subtest("gem_multiple_process") {
		gem_multiple_process_test();
	}
	igt_subtest("gem_repin") {
		gem_repin_test();
	}
	igt_subtest("gem_pin_overlap") {
		gem_pin_overlap_test();
	}
	igt_subtest("gem_write_multipage_buffer") {
		gem_write_multipage_buffer_test();
	}

	/* Following tests need 32/48 Bit PPGTT support */
	igt_subtest("gem_pin_invalid_vma") {
		gem_pin_invalid_vma_test(false, false);
	}

	/* Following tests need 48 Bit PPGTT support */
	igt_subtest("gen_pin_noncanonical_high_address") {
		gem_pin_invalid_vma_test(false, true);
	}
	igt_subtest("gem_pin_high_address_without_correct_flag") {
		gem_pin_invalid_vma_test(true, false);
	}
	igt_subtest("gem_softpin_stress") {
		gem_softpin_stress_test();
	}
	igt_subtest("gem_pin_high_address") {
		gem_pin_high_address_test();
	}
	igt_subtest("gem_pin_near_48Bit") {
		gem_pin_near_48Bit_test();
	}

	igt_exit();
}

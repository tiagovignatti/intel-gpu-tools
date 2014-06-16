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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

#ifndef IGT_AUX_H
#define IGT_AUX_H

#include <intel_bufmgr.h>
#include <stdbool.h>

/* auxialiary igt helpers from igt_aux.c */
/* generally useful helpers */
void igt_fork_signal_helper(void);
void igt_stop_signal_helper(void);
void igt_exchange_int(void *array, unsigned i, unsigned j);
void igt_permute_array(void *array, unsigned size,
			   void (*exchange_func)(void *array,
						 unsigned i,
						 unsigned j));
void igt_progress(const char *header, uint64_t i, uint64_t total);
bool igt_check_boolean_env_var(const char *env_var, bool default_value);

bool igt_aub_dump_enabled(void);

/* helpers based upon the libdrm buffer manager */
void igt_init_aperture_trashers(drm_intel_bufmgr *bufmgr);
void igt_trash_aperture(void);
void igt_cleanup_aperture_trashers(void);

/* suspend and auto-resume system */
void igt_system_suspend_autoresume(void);

/* dropping priviledges */
void igt_drop_root(void);

void igt_wait_for_keypress(void);

enum igt_runtime_pm_status {
	IGT_RUNTIME_PM_STATUS_ACTIVE,
	IGT_RUNTIME_PM_STATUS_SUSPENDED,
	IGT_RUNTIME_PM_STATUS_SUSPENDING,
	IGT_RUNTIME_PM_STATUS_RESUMING,
	IGT_RUNTIME_PM_STATUS_UNKNOWN,
};
bool igt_setup_runtime_pm(void);
enum igt_runtime_pm_status igt_get_runtime_pm_status(void);
bool igt_wait_for_pm_status(enum igt_runtime_pm_status status);

/* sysinfo cross-arch wrappers from intel_os.c */

/* These are separate to allow easier testing when porting, see the comment at
 * the bottom of intel_os.c. */
void intel_purge_vm_caches(void);
uint64_t intel_get_avail_ram_mb(void);
uint64_t intel_get_total_ram_mb(void);
uint64_t intel_get_total_swap_mb(void);

bool intel_check_memory(uint32_t count, uint32_t size, unsigned mode);
#define CHECK_RAM 0x1
#define CHECK_SWAP 0x2

#endif /* IGT_AUX_H */

/*
 * Copyright © 2014, 2015 Intel Corporation
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
#include <sys/time.h>

extern drm_intel_bo **trash_bos;
extern int num_trash_bos;

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
void igt_print_activity(void);
bool igt_check_boolean_env_var(const char *env_var, bool default_value);

bool igt_aub_dump_enabled(void);

/* helpers based upon the libdrm buffer manager */
void igt_init_aperture_trashers(drm_intel_bufmgr *bufmgr);
void igt_trash_aperture(void);
void igt_cleanup_aperture_trashers(void);

/* suspend/hibernate and auto-resume system */
void igt_system_suspend_autoresume(void);
void igt_system_hibernate_autoresume(void);

/* dropping priviledges */
void igt_drop_root(void);

void igt_debug_wait_for_keypress(const char *var);
void igt_debug_manual_check(const char *var, const char *expected);

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

void intel_require_memory(uint32_t count, uint32_t size, unsigned mode);
#define CHECK_RAM 0x1
#define CHECK_SWAP 0x2


#define min(a, b) ({			\
	typeof(a) _a = (a);		\
	typeof(b) _b = (b);		\
	_a < _b ? _a : _b;		\
})
#define max(a, b) ({			\
	typeof(a) _a = (a);		\
	typeof(b) _b = (b);		\
	_a > _b ? _a : _b;		\
})

#define igt_swap(a, b) do {	\
	typeof(a) _tmp = (a);	\
	(a) = (b);		\
	(b) = _tmp;		\
} while (0)

void igt_lock_mem(size_t size);
void igt_unlock_mem(void);

/**
 * igt_wait:
 * @COND: condition to wait
 * @timeout_ms: timeout in milliseconds
 * @interval_ms: amount of time we try to sleep between COND checks
 *
 * Waits until COND evaluates to true or the timeout passes.
 *
 * It is safe to call this macro if the signal helper is active. The only
 * problem is that the usleep() calls will return early, making us evaluate COND
 * too often, possibly eating valuable CPU cycles.
 *
 * Returns:
 * True of COND evaluated to true, false otherwise.
 */
#define igt_wait(COND, timeout_ms, interval_ms) ({			\
	struct timeval start_, end_, diff_;				\
	int elapsed_ms_;						\
	bool ret_ = false;						\
									\
	igt_assert(gettimeofday(&start_, NULL) == 0);			\
	do {								\
		if (COND) {						\
			ret_ = true;					\
			break;						\
		}							\
									\
		usleep(interval_ms * 1000);				\
									\
		igt_assert(gettimeofday(&end_, NULL) == 0);		\
		timersub(&end_, &start_, &diff_);			\
									\
		elapsed_ms_ = diff_.tv_sec * 1000 +			\
			      diff_.tv_usec / 1000;			\
	} while (elapsed_ms_ < timeout_ms);				\
									\
	if (!ret_ && (COND))						\
		ret_ = true;						\
									\
	ret_;								\
})


void igt_set_module_param(const char *name, const char *val);
void igt_set_module_param_int(const char *name, int val);

#endif /* IGT_AUX_H */

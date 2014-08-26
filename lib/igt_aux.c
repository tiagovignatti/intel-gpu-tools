/*
 * Copyright © 2007, 2011, 2013, 2014 Intel Corporation
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
 *    Eric Anholt <eric@anholt.net>
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

#ifndef ANDROID
#define _GNU_SOURCE
#else
#include <libgen.h>
#endif
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sys/mman.h>
#include <signal.h>
#include <pciaccess.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <termios.h>

#include "drmtest.h"
#include "i915_drm.h"
#include "intel_chipset.h"
#include "igt_aux.h"
#include "igt_debugfs.h"
#include "config.h"
#include "intel_reg.h"
#include "ioctl_wrappers.h"


/**
 * SECTION:igt_aux
 * @short_description: Auxiliary libararies and support functions
 * @title: i-g-t aux
 * @include: igt_aux.h
 *
 * This library provides various auxiliary helper functions that don't really
 * fit into any other topic.
 */


/* signal interrupt helpers */
static struct igt_helper_process signal_helper;
long long int sig_stat;
static void __attribute__((noreturn)) signal_helper_process(pid_t pid)
{
	/* Interrupt the parent process at 500Hz, just to be annoying */
	while (1) {
		usleep(1000 * 1000 / 500);
		if (kill(pid, SIGUSR1)) /* Parent has died, so must we. */
			exit(0);
	}
}

static void sig_handler(int i)
{
	sig_stat++;
}

/**
 * igt_fork_signal_helper:
 *
 * Fork a child process using #igt_fork_helper to interrupt the parent process
 * with a SIGUSR1 signal at regular quick intervals. The corresponding dummy
 * signal handler is installed in the parent process.
 *
 * This is useful to exercise ioctl error paths, at least where those can be
 * exercises by interrupting blocking waits, like stalling for the gpu. This
 * helper can also be used from children spawned with #igt_fork.
 *
 * In tests with subtests this function can be called outside of failure
 * catching code blocks like #igt_fixture or #igt_subtest.
 */
void igt_fork_signal_helper(void)
{
	if (igt_only_list_subtests())
		return;

	signal(SIGUSR1, sig_handler);

	igt_fork_helper(&signal_helper) {
		signal_helper_process(getppid());
	}
}

/**
 * igt_stop_signal_helper:
 *
 * Stops the child process spawned with igt_fork_signal_helper() again.
 *
 * In tests with subtests this function can be called outside of failure
 * catching code blocks like #igt_fixture or #igt_subtest.
 */
void igt_stop_signal_helper(void)
{
	if (igt_only_list_subtests())
		return;

	igt_stop_helper(&signal_helper);

	sig_stat = 0;
}

/**
 * igt_check_boolean_env_var:
 * @env_var: environment variable name
 * @default_value: default value for the environment variable
 *
 * This function should be used to parse boolean environment variable options.
 *
 * Returns:
 * The boolean value of the environment variable @env_var as decoded by atoi()
 * if it is set and @default_value if the variable is not set.
 */
bool igt_check_boolean_env_var(const char *env_var, bool default_value)
{
	char *val;

	val = getenv(env_var);
	if (!val)
		return default_value;

	return atoi(val) != 0;
}

/**
 * igt_aub_dump_enabled:
 *
 * Returns:
 * True if AUB dumping is enabled with IGT_DUMP_AUB=1 in the environment, false
 * otherwise.
 */
bool igt_aub_dump_enabled(void)
{
	static int dump_aub = -1;

	if (dump_aub == -1)
		dump_aub = igt_check_boolean_env_var("IGT_DUMP_AUB", false);

	return dump_aub;
}

/* other helpers */
/**
 * igt_exchange_int:
 * @array: pointer to the array of integers
 * @i: first position
 * @j: second position
 *
 * Exchanges the two values at array indices @i and @j. Useful as an exchange
 * function for igt_permute_array().
 */
void igt_exchange_int(void *array, unsigned i, unsigned j)
{
	int *int_arr, tmp;
	int_arr = array;

	tmp = int_arr[i];
	int_arr[i] = int_arr[j];
	int_arr[j] = tmp;
}

/**
 * igt_permute_array:
 * @array: pointer to array
 * @size: size of the array
 * @exchange_func: function to exchange array elements
 *
 * This function randomly permutes the array using random() as the PRNG source.
 * The @exchange_func function is called to exchange two elements in the array
 * when needed.
 */
void igt_permute_array(void *array, unsigned size,
                       void (*exchange_func)(void *array,
                                             unsigned i,
                                             unsigned j))
{
	int i;

	for (i = size - 1; i > 1; i--) {
		/* yes, not perfectly uniform, who cares */
		long l = random() % (i +1);
		if (i != l)
			exchange_func(array, i, l);
	}
}

/**
 * igt_progress:
 * @header: header string to prepend to the progress indicator
 * @i: work processed thus far
 * @total: total amount of work
 *
 * This function draws a progress indicator, which is useful for running
 * long-winded tests manually on the console. To avoid spamming logfiles in
 * automated runs the progress indicator is supressed when not running on a
 * terminal.
 */
void igt_progress(const char *header, uint64_t i, uint64_t total)
{
	int divider = 200;

	if (!isatty(fileno(stderr)))
		return;

	if (i+1 >= total) {
		igt_warn("\r%s100%%\n", header);
		return;
	}

	if (total / 200 == 0)
		divider = 1;

	/* only bother updating about every 0.5% */
	igt_warn_on_f(i % (total / divider) == 0 || i + 1 >= total,
		      "\r%s%3llu%%", header,
		      (long long unsigned)i * 100 / total);
}

/* mappable aperture trasher helper */
drm_intel_bo **trash_bos;
int num_trash_bos;

/**
 * igt_init_aperture_trashers:
 * @bufmgr: libdrm buffer manager
 *
 * Initialize the aperture trasher using @bufmgr, which can then be run with
 * igt_trash_aperture().
 */
void igt_init_aperture_trashers(drm_intel_bufmgr *bufmgr)
{
	int i;

	num_trash_bos = gem_mappable_aperture_size() / (1024*1024);

	trash_bos = malloc(num_trash_bos * sizeof(drm_intel_bo *));
	igt_assert(trash_bos);

	for (i = 0; i < num_trash_bos; i++)
		trash_bos[i] = drm_intel_bo_alloc(bufmgr, "trash bo", 1024*1024, 4096);
}

/**
 * igt_trash_aperture:
 *
 * Trash the aperture by walking a set of GTT memory mapped objects.
 */
void igt_trash_aperture(void)
{
	int i;
	uint8_t *gtt_ptr;

	for (i = 0; i < num_trash_bos; i++) {
		drm_intel_gem_bo_map_gtt(trash_bos[i]);
		gtt_ptr = trash_bos[i]->virtual;
		*gtt_ptr = 0;
		drm_intel_gem_bo_unmap_gtt(trash_bos[i]);
	}
}

/**
 * igt_cleanup_aperture_trashers:
 *
 * Clean up all aperture trasher state set up with igt_init_aperture_trashers().
 */
void igt_cleanup_aperture_trashers(void)
{
	int i;

	for (i = 0; i < num_trash_bos; i++)
		drm_intel_bo_unreference(trash_bos[i]);

	free(trash_bos);
}

/**
 * igt_system_suspend_autoresume:
 *
 * Execute a system suspend-to-mem cycle and automatically wake up again using
 * the firmwares resume timer.
 *
 * This is very handy for implementing any kind of suspend/resume test.
 */
void igt_system_suspend_autoresume(void)
{
	int ret;

	/* FIXME: Simulation doesn't like suspend/resume, and not even a lighter
	 * approach using /sys/power/pm_test to just test our driver's callbacks
	 * seems to fare better. We need to investigate what's going on. */
	igt_skip_on_simulation();

	ret = system("rtcwake -s 30 -m mem");
	igt_assert(ret == 0);
}

/**
 * igt_drop_roo:
 *
 * Drop root priviledges and make sure it actually worked. Useful for tests
 * which need to check security constraints. Note that this should only be
 * called from manually forked processes, since the lack of root priviledges
 * will wreak havoc with the automatic cleanup handlers.
 */
void igt_drop_root(void)
{
	igt_assert(getuid() == 0);

	igt_assert(setgid(2) == 0);
	igt_assert(setuid(2) == 0);

	igt_assert(getgid() == 2);
	igt_assert(getuid() == 2);
}

/**
 * igt_wait_for_keypress:
 *
 * Waits for a key press when run interactively. When not connected to a
 * terminal immediately continues.
 *
 * This is useful for display tests where under certain situation manual
 * inspection of the display is useful.
 */
void igt_wait_for_keypress(void)
{
	struct termios oldt, newt;

	if (!isatty(STDIN_FILENO))
		return;

	tcgetattr ( STDIN_FILENO, &oldt );
	newt = oldt;
	newt.c_lflag &= ~( ICANON | ECHO );
	tcsetattr ( STDIN_FILENO, TCSANOW, &newt );
	getchar();
	tcsetattr ( STDIN_FILENO, TCSANOW, &oldt );
}

#define POWER_DIR "/sys/devices/pci0000:00/0000:00:02.0/power"
/* We just leak this on exit ... */
int pm_status_fd = -1;

/**
 * igt_setup_runtime_pm:
 *
 * Sets up the runtime PM helper functions and enables runtime PM. To speed up
 * tests the autosuspend delay is set to 0.
 *
 * Returns:
 * True if runtime pm is available, false otherwise.
 */
bool igt_setup_runtime_pm(void)
{
	int fd;
	ssize_t size;
	char buf[6];

	if (pm_status_fd >= 0)
		return true;

	/* Our implementation uses autosuspend. Try to set it to 0ms so the test
	 * suite goes faster and we have a higher probability of triggering race
	 * conditions. */
	fd = open(POWER_DIR "/autosuspend_delay_ms", O_WRONLY);
	igt_assert_f(fd >= 0,
		     "Can't open " POWER_DIR "/autosuspend_delay_ms\n");

	/* If we fail to write to the file, it means this system doesn't support
	 * runtime PM. */
	size = write(fd, "0\n", 2);

	close(fd);

	if (size != 2)
		return false;

	/* We know we support runtime PM, let's try to enable it now. */
	fd = open(POWER_DIR "/control", O_RDWR);
	igt_assert_f(fd >= 0, "Can't open " POWER_DIR "/control\n");

	size = write(fd, "auto\n", 5);
	igt_assert(size == 5);

	lseek(fd, 0, SEEK_SET);
	size = read(fd, buf, ARRAY_SIZE(buf));
	igt_assert(size == 5);
	igt_assert(strncmp(buf, "auto\n", 5) == 0);

	close(fd);

	pm_status_fd = open(POWER_DIR "/runtime_status", O_RDONLY);
	igt_assert_f(pm_status_fd >= 0,
		     "Can't open " POWER_DIR "/runtime_status\n");

	return true;
}

/**
 * igt_get_runtime_pm_status:
 *
 * Returns: The current runtime PM status.
 */
enum igt_runtime_pm_status igt_get_runtime_pm_status(void)
{
	ssize_t n_read;
	char buf[32];

	lseek(pm_status_fd, 0, SEEK_SET);
	n_read = read(pm_status_fd, buf, ARRAY_SIZE(buf));
	igt_assert(n_read >= 0);
	buf[n_read] = '\0';

	if (strncmp(buf, "suspended\n", n_read) == 0)
		return IGT_RUNTIME_PM_STATUS_SUSPENDED;
	else if (strncmp(buf, "active\n", n_read) == 0)
		return IGT_RUNTIME_PM_STATUS_ACTIVE;
	else if (strncmp(buf, "suspending\n", n_read) == 0)
		return IGT_RUNTIME_PM_STATUS_SUSPENDING;
	else if (strncmp(buf, "resuming\n", n_read) == 0)
		return IGT_RUNTIME_PM_STATUS_RESUMING;

	igt_assert_f(false, "Unknown status %s\n", buf);
	return IGT_RUNTIME_PM_STATUS_UNKNOWN;
}

/**
 * igt_wait_for_pm_status:
 * @status: desired runtime PM status
 *
 * Waits until for the driver to switch to into the desired runtime PM status,
 * with a 10 second timeout.
 *
 * Returns:
 * True if the desired runtime PM status was attained, false if the operation
 * timed out.
 */
bool igt_wait_for_pm_status(enum igt_runtime_pm_status status)
{
	int i;
	int hundred_ms = 100 * 1000, ten_s = 10 * 1000 * 1000;

	for (i = 0; i < ten_s; i += hundred_ms) {
		if (igt_get_runtime_pm_status() == status)
			return true;

		usleep(hundred_ms);
	}

	return false;
}

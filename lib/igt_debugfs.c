/*
 * Copyright © 2013 Intel Corporation
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

#include <inttypes.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <i915_drm.h>

#include "drmtest.h"
#include "igt_kms.h"
#include "igt_debugfs.h"

/**
 * SECTION:igt_debugfs
 * @short_description: Support code for debugfs features
 * @title: i-g-t debugfs
 * @include: igt_debugfs.h
 *
 * This library provides helpers to access debugfs features. On top of some
 * basic functions to access debugfs files with e.g. igt_debugfs_open() it also
 * provides higher-level wrappers for some debugfs features
 *
 * # Pipe CRC Support
 *
 * This library wraps up the kernel's support for capturing pipe CRCs into a
 * neat and tidy package. For the datailed usage see all the functions which
 * work on #igt_pipe_crc_t. This is supported on all platforms and outputs.
 *
 * Actually using pipe CRCs to write modeset tests is a bit tricky though, so
 * there is no way to directly check a CRC: Both the details of the plane
 * blending, color correction and other hardware and how exactly the CRC is
 * computed at each tap point vary by hardware generation and are not disclosed.
 *
 * The only way to use #igt_crc_t CRCs therefors is to compare CRCs among each
 * another either for equality or difference. Otherwise CRCs must be treated as
 * completely opaque values. Note that not even CRCs from different pipes or tap
 * points on the same platform can be compared. Hence only use igt_crc_is_null()
 * and igt_crc_equal() to inspect CRC values captured by the same
 * #igt_pipe_crc_t object.
 *
 * # Other debugfs interface wrappers
 *
 * This covers the miscellaneous debugfs interface wrappers:
 *
 * - drm/i915 supports interfaces to evict certain clases of gem buffer objects,
 *   see igt_drop_caches_set().
 *
 * - drm/i915 supports an interface to disable prefaulting, useful to test
 *   slowpaths in ioctls. See igt_disable_prefault().
 */

/*
 * General debugfs helpers
 */

typedef struct {
	char root[128];
	char dri_path[128];
} igt_debugfs_t;

static bool __igt_debugfs_init(igt_debugfs_t *debugfs)
{
	const char *path = "/sys/kernel/debug";
	struct stat st;
	int n;

	if (stat("/debug/dri", &st) == 0) {
		path = "/debug/dri";
		goto find_minor;
	}

	if (stat("/sys/kernel/debug/dri", &st) == 0)
		goto find_minor;

	igt_assert(stat("/sys/kernel/debug", &st) == 0);

	mount("debug", "/sys/kernel/debug", "debugfs", 0, 0);

find_minor:
	strcpy(debugfs->root, path);
	for (n = 0; n < 16; n++) {
		int len = sprintf(debugfs->dri_path, "%s/dri/%d", path, n);
		sprintf(debugfs->dri_path + len, "/i915_error_state");
		if (stat(debugfs->dri_path, &st) == 0) {
			debugfs->dri_path[len] = '\0';
			return true;
		}
	}

	debugfs->dri_path[0] = '\0';

	return false;
}

static igt_debugfs_t *__igt_debugfs_singleton(void)
{
	static igt_debugfs_t singleton;
	static bool init_done = false;

	if (init_done)
		return &singleton;

	if (__igt_debugfs_init(&singleton)) {
		init_done = true;
		return &singleton;
	} else {
		return NULL;
	}
}

/**
 * igt_debugfs_open:
 * @filename: name of the debugfs node to open
 * @mode: mode bits as used by open()
 *
 * This opens a debugfs file as a Unix file descriptor. The filename should be
 * relative to the drm device's root, i.e without "drm/&lt;minor&gt;".
 *
 * Returns:
 * The Unix file descriptor for the debugfs file or -1 if that didn't work out.
 */
int igt_debugfs_open(const char *filename, int mode)
{
	char buf[1024];
	igt_debugfs_t *debugfs = __igt_debugfs_singleton();

	if (!debugfs)
		return -1;

	sprintf(buf, "%s/%s", debugfs->dri_path, filename);
	return open(buf, mode);
}

/**
 * igt_debugfs_fopen:
 * @filename: name of the debugfs node to open
 * @mode: mode string as used by fopen()
 *
 * This opens a debugfs file as a libc FILE. The filename should be
 * relative to the drm device's root, i.e without "drm/&lt;minor&gt;".
 *
 * Returns:
 * The libc FILE pointer for the debugfs file or NULL if that didn't work out.
 */
FILE *igt_debugfs_fopen(const char *filename,
			const char *mode)
{
	char buf[1024];

	igt_debugfs_t *debugfs = __igt_debugfs_singleton();

	if (!debugfs)
		return NULL;

	sprintf(buf, "%s/%s", debugfs->dri_path, filename);
	return fopen(buf, mode);
}

/*
 * Pipe CRC
 */

/**
 * igt_crc_is_null:
 * @crc: pipe CRC value to check
 *
 * Returns: True if the CRC is null/invalid, false if it represents a captured
 * valid CRC.
 */
bool igt_crc_is_null(igt_crc_t *crc)
{
	int i;

	for (i = 0; i < crc->n_words; i++) {
		igt_warn_on_f(crc->crc[i] == 0xffffffff,
			      "Suspicious CRC: it looks like the CRC "
			      "read back was from a register in a powered "
			      "down well\n");
		if (crc->crc[i])
			return false;
	}

	return true;
}

/**
 * igt_crc_equal:
 * @a: first pipe CRC value
 * @b: second pipe CRC value
 *
 * Compares two CRC values.
 *
 * Returns: Retruns true if the two CRCs match, false otherwise.
 */
bool igt_crc_equal(igt_crc_t *a, igt_crc_t *b)
{
	int i;

	if (a->n_words != b->n_words)
		return false;

	for (i = 0; i < a->n_words; i++)
		if (a->crc[i] != b->crc[i])
			return false;

	return true;
}

/**
 * igt_crc_to_string:
 * @crc: pipe CRC value to print
 *
 * This formats @crc into a string buffer which is owned by igt_crc_to_string().
 * The next call will override the buffer again, which makes this multithreading
 * unsafe.
 *
 * This should only ever be used for diagnostic debug output.
 */
char *igt_crc_to_string(igt_crc_t *crc)
{
	char buf[128];

	if (crc->n_words == 5)
		sprintf(buf, "%08x %08x %08x %08x %08x", crc->crc[0],
			crc->crc[1], crc->crc[2], crc->crc[3], crc->crc[4]);
	else
		igt_assert(0);

	return strdup(buf);
}

/* (6 fields, 8 chars each, space separated (5) + '\n') */
#define PIPE_CRC_LINE_LEN       (6 * 8 + 5 + 1)
/* account for \'0' */
#define PIPE_CRC_BUFFER_LEN     (PIPE_CRC_LINE_LEN + 1)

struct _igt_pipe_crc {
	int ctl_fd;
	int crc_fd;
	int line_len;
	int buffer_len;

	enum pipe pipe;
	enum intel_pipe_crc_source source;
};

static const char *pipe_crc_sources[] = {
	"none",
	"plane1",
	"plane2",
	"pf",
	"pipe",
	"TV",
	"DP-B",
	"DP-C",
	"DP-D",
	"auto"
};

static const char *pipe_crc_source_name(enum intel_pipe_crc_source source)
{
        return pipe_crc_sources[source];
}

static bool igt_pipe_crc_do_start(igt_pipe_crc_t *pipe_crc)
{
	char buf[64];

	sprintf(buf, "pipe %s %s", kmstest_pipe_name(pipe_crc->pipe),
		pipe_crc_source_name(pipe_crc->source));
	errno = 0;
	write(pipe_crc->ctl_fd, buf, strlen(buf));
	if (errno != 0)
		return false;

	return true;
}

static void igt_pipe_crc_pipe_off(int fd, enum pipe pipe)
{
	char buf[32];

	sprintf(buf, "pipe %s none", kmstest_pipe_name(pipe));
	write(fd, buf, strlen(buf));
}

static void igt_pipe_crc_reset(void)
{
	int fd;

	fd = igt_debugfs_open("i915_display_crc_ctl", O_WRONLY);

	igt_pipe_crc_pipe_off(fd, PIPE_A);
	igt_pipe_crc_pipe_off(fd, PIPE_B);
	igt_pipe_crc_pipe_off(fd, PIPE_C);

	close(fd);
}

static void pipe_crc_exit_handler(int sig)
{
	igt_pipe_crc_reset();
}

/**
 * igt_require_pipe_crc:
 *
 * Convenience helper to check whether pipe CRC capturing is supported by the
 * kernel. Uses igt_skip to automatically skip the test/subtest if this isn't
 * the case.
 */
void igt_require_pipe_crc(void)
{
	const char *cmd = "pipe A none";
	FILE *ctl;
	size_t written;
	int ret;

	ctl = igt_debugfs_fopen("i915_display_crc_ctl", "r+");
	igt_require_f(ctl,
		      "No display_crc_ctl found, kernel too old\n");
	written = fwrite(cmd, 1, strlen(cmd), ctl);
	ret = fflush(ctl);
	igt_require_f((written == strlen(cmd) && ret == 0) || errno != ENODEV,
		      "CRCs not supported on this platform\n");

	fclose(ctl);
}

/**
 * igt_pipe_crc_new:
 * @pipe: display pipe to use as source
 * @source: CRC tap point to use as source
 *
 * This sets up a new pipe CRC capture object for the given @pipe and @source.
 *
 * Returns: A pipe CRC object if the given @pipe and @source is available, NULL
 * otherwise. Tests can use this to intelligently skip if they require a
 * specific pipe CRC source to function properly.
 */
igt_pipe_crc_t *
igt_pipe_crc_new(enum pipe pipe, enum intel_pipe_crc_source source)
{
	igt_pipe_crc_t *pipe_crc;
	char buf[128];

	igt_install_exit_handler(pipe_crc_exit_handler);

	pipe_crc = calloc(1, sizeof(struct _igt_pipe_crc));

	pipe_crc->ctl_fd = igt_debugfs_open("i915_display_crc_ctl", O_WRONLY);
	igt_assert(pipe_crc->ctl_fd != -1);

	sprintf(buf, "i915_pipe_%s_crc", kmstest_pipe_name(pipe));
	pipe_crc->crc_fd = igt_debugfs_open(buf, O_RDONLY);
	igt_assert(pipe_crc->crc_fd != -1);

	pipe_crc->line_len = PIPE_CRC_LINE_LEN;
	pipe_crc->buffer_len = PIPE_CRC_BUFFER_LEN;
	pipe_crc->pipe = pipe;
	pipe_crc->source = source;

	/* make sure this source is actually supported */
	if (!igt_pipe_crc_do_start(pipe_crc)) {
		igt_pipe_crc_free(pipe_crc);
		return NULL;
	}

	igt_pipe_crc_stop(pipe_crc);

	return pipe_crc;
}

/**
 * igt_pipe_crc_free:
 * @pipe_crc: pipe CRC object
 *
 * Frees all resources associated with @pipe_crc.
 */
void igt_pipe_crc_free(igt_pipe_crc_t *pipe_crc)
{
	if (!pipe_crc)
		return;

	close(pipe_crc->ctl_fd);
	close(pipe_crc->crc_fd);
	free(pipe_crc);
}

/**
 * igt_pipe_crc_start:
 * @pipe_crc: pipe CRC object
 *
 * Starts the CRC capture process on @pipe_crc.
 */
void igt_pipe_crc_start(igt_pipe_crc_t *pipe_crc)
{
	igt_crc_t *crcs = NULL;

	igt_assert(igt_pipe_crc_do_start(pipe_crc));

	/*
	 * For some no yet identified reason, the first CRC is bonkers. So
	 * let's just wait for the next vblank and read out the buggy result.
	 */
	igt_pipe_crc_get_crcs(pipe_crc, 1, &crcs);
	free(crcs);
}

/**
 * igt_pipe_crc_stop:
 * @pipe_crc: pipe CRC object
 *
 * Stops the CRC capture process on @pipe_crc.
 */
void igt_pipe_crc_stop(igt_pipe_crc_t *pipe_crc)
{
	char buf[32];

	sprintf(buf, "pipe %s none", kmstest_pipe_name(pipe_crc->pipe));
	write(pipe_crc->ctl_fd, buf, strlen(buf));
}

static bool pipe_crc_init_from_string(igt_crc_t *crc, const char *line)
{
	int n;

	crc->n_words = 5;
	n = sscanf(line, "%8u %8x %8x %8x %8x %8x", &crc->frame, &crc->crc[0],
		   &crc->crc[1], &crc->crc[2], &crc->crc[3], &crc->crc[4]);
	return n == 6;
}

static bool read_one_crc(igt_pipe_crc_t *pipe_crc, igt_crc_t *out)
{
	ssize_t bytes_read;
	char buf[pipe_crc->buffer_len];

	igt_set_timeout(5);
	bytes_read = read(pipe_crc->crc_fd, &buf, pipe_crc->line_len);
	igt_set_timeout(0);

	igt_assert_cmpint(bytes_read, ==, pipe_crc->line_len);
	buf[bytes_read] = '\0';

	if (!pipe_crc_init_from_string(out, buf))
		return false;

	return true;
}

/**
 * igt_pipe_crc_get_crcs:
 * @pipe_crc: pipe CRC object
 * @n_crcs: number of CRCs to capture
 * @out_crcs: buffer pointer for the captured CRC values
 *
 * Read @n_crcs from @pipe_crc. This function blocks until @n_crcs are
 * retrieved. @out_crcs is alloced by this function and must be released with
 * free() by the caller.
 *
 * Callers must start and stop the capturing themselves by calling
 * igt_pipe_crc_start() and igt_pipe_crc_stop().
 */
void
igt_pipe_crc_get_crcs(igt_pipe_crc_t *pipe_crc, int n_crcs,
		      igt_crc_t **out_crcs)
{
	igt_crc_t *crcs;
	int n = 0;

	crcs = calloc(n_crcs, sizeof(igt_crc_t));

	do {
		igt_crc_t *crc = &crcs[n];

		if (!read_one_crc(pipe_crc, crc))
			continue;

		n++;
	} while (n < n_crcs);

	*out_crcs = crcs;
}

/**
 * igt_pipe_crc_collect_crc:
 * @pipe_crc: pipe CRC object
 * @out_crc: buffer for the captured CRC values
 *
 * Read a single CRC from @pipe_crc. This function blocks until the CRC is
 * retrieved.  @out_crc must be allocated by the caller.
 *
 * This function takes care of the pipe_crc book-keeping, it will start/stop
 * the collection of the CRC.
 */
void igt_pipe_crc_collect_crc(igt_pipe_crc_t *pipe_crc, igt_crc_t *out_crc)
{
	igt_pipe_crc_start(pipe_crc);
	read_one_crc(pipe_crc, out_crc);
	igt_pipe_crc_stop(pipe_crc);

	igt_assert(!igt_crc_is_null(out_crc));
}

/*
 * Drop caches
 */

/**
 * igt_drop_caches_set:
 * @val: bitmask for DROP_* values
 *
 * This calls the debugfs interface the drm/i915 GEM driver exposes to drop or
 * evict certain classes of gem buffer objects.
 */
void igt_drop_caches_set(uint64_t val)
{
	int fd;
	char data[19];
	size_t nbytes;

	sprintf(data, "0x%" PRIx64, val);

	fd = igt_debugfs_open("i915_gem_drop_caches", O_WRONLY);

	igt_assert(fd >= 0);
	nbytes = write(fd, data, strlen(data) + 1);
	igt_assert(nbytes == strlen(data) + 1);
	close(fd);
}

/*
 * Prefault control
 */

#define PREFAULT_DEBUGFS "/sys/module/i915/parameters/prefault_disable"
static void igt_prefault_control(bool enable)
{
	const char *name = PREFAULT_DEBUGFS;
	int fd;
	char buf[2] = {'Y', 'N'};
	int index;

	fd = open(name, O_RDWR);
	igt_require(fd >= 0);

	if (enable)
		index = 1;
	else
		index = 0;

	igt_require(write(fd, &buf[index], 1) == 1);

	close(fd);
}

static void enable_prefault_at_exit(int sig)
{
	igt_enable_prefault();
}

/**
 * igt_disable_prefault:
 *
 * Disable prefaulting in certain gem ioctls through the debugfs interface. As
 * usual this installs an exit handler to clean up and re-enable prefaulting
 * even when the test exited abnormally.
 *
 * igt_enable_prefault() will enable normale operation again.
 */
void igt_disable_prefault(void)
{
	igt_prefault_control(false);

	igt_install_exit_handler(enable_prefault_at_exit);
}

/**
 * igt_enable_prefault:
 *
 * Enable prefault (again) through the debugfs interface.
 */
void igt_enable_prefault(void)
{
	igt_prefault_control(true);
}

/**
 * igt_open_forcewake_handle:
 *
 * This functions opens the debugfs forcewake file and so prevents the GT from
 * suspending. The reference is automatically dropped when the is closed.
 *
 * Returns:
 * The file descriptor of the forcewake handle or -1 if that didn't work out.
 */
int igt_open_forcewake_handle(void)
{
	if (getenv("IGT_NO_FORCEWAKE"))
		return -1;
	return igt_debugfs_open("i915_forcewake_user", O_WRONLY);
}

/**
 * igt_to_stop_ring_flag:
 * @ring: the specified ring flag from execbuf ioctl (I915_EXEC_*)
 *
 * This converts the specified ring to a ring flag to be used
 * with igt_get_stop_rings() and igt_set_stop_rings().
 *
 * Returns:
 * Ring flag for the given ring.
 */
enum stop_ring_flags igt_to_stop_ring_flag(int ring) {
	if (ring == I915_EXEC_DEFAULT)
		return STOP_RING_RENDER;

	igt_assert(ring && ((ring & ~I915_EXEC_RING_MASK) == 0));
	return 1 << (ring - 1);
}

static void stop_rings_write(uint32_t mask)
{
	int fd;
	char buf[80];

	igt_assert(snprintf(buf, sizeof(buf), "0x%08x", mask) == 10);
	fd = igt_debugfs_open("i915_ring_stop", O_WRONLY);
	igt_assert(fd >= 0);

	igt_assert(write(fd, buf, strlen(buf)) == strlen(buf));
	close(fd);
}

/**
 * igt_get_stop_rings:
 *
 * Read current ring flags from 'i915_ring_stop' debugfs entry.
 *
 * Returns:
 * Current ring flags.
 */
enum stop_ring_flags igt_get_stop_rings(void)
{
	int fd;
	char buf[80];
	int l;
	unsigned long long ring_mask;

	fd = igt_debugfs_open("i915_ring_stop", O_RDONLY);
	igt_assert(fd >= 0);
	l = read(fd, buf, sizeof(buf)-1);
	igt_assert(l > 0);
	igt_assert(l < sizeof(buf));

	buf[l] = '\0';

	close(fd);

	errno = 0;
	ring_mask = strtoull(buf, NULL, 0);
	igt_assert(errno == 0);
	return ring_mask;
}

/**
 * igt_set_stop_rings:
 * @flags: Ring flags to write
 *
 * This writes @flags to 'i915_ring_stop' debugfs entry. Driver will
 * prevent the CPU from writing tail pointer for the ring that @flags
 * specify. Note that the ring is not stopped right away. Instead any
 * further command emissions won't be executed after the flag is set.
 *
 * This is the least invasive way to make the GPU stuck. Hence you must
 * set this after a batch submission with it's own invalid or endless
 * looping instructions. In this case it is merely for giving notification
 * for the driver that this was simulated hang, as the batch would have
 * caused hang in any case. On the other hand if you use a valid or noop
 * batch and want to hang the ring (GPU), you must set corresponding flag
 * before submitting the batch.
 *
 * Driver checks periodically if a ring is making any progress, and if
 * it is not, it will declare the ring to be hung and will reset the GPU.
 * After reset, the driver will clear flags in 'i915_ring_stop'
 *
 * Note: Always when hanging the GPU, use igt_set_stop_rings() to
 * notify the driver. Driver controls hang log messaging based on
 * these flags and thus prevents false positives on logs.
 */
void igt_set_stop_rings(enum stop_ring_flags flags)
{
	enum stop_ring_flags current;

	igt_assert((flags & ~(STOP_RING_ALL |
			      STOP_RING_ALLOW_BAN |
			      STOP_RING_ALLOW_ERRORS)) == 0);

	current = igt_get_stop_rings();
	igt_assert_f(flags == 0 || current == 0,
		     "previous i915_ring_stop is still 0x%x\n", current);

	stop_rings_write(flags);
	current = igt_get_stop_rings();
	igt_warn_on_f(current != flags,
		      "i915_ring_stop readback mismatch 0x%x vs 0x%x\n",
		      flags, current);
}

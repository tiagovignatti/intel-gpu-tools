/*
 * Copyright (c) 2012 Intel Corporation
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
 *    Mika Kuoppala <mika.kuoppala@intel.com>
 *
 */

/*
 * This test runs blitcopy -> rendercopy with multiple buffers over wrap
 * boundary. Note: Driver can only handle UINT32_MAX/2-1 increments to seqno.
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <limits.h>
#include <wordexp.h>

#include "i915_drm.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_gpu_tools.h"
#include "rendercopy.h"

#define BUFFERS_TO_SYNC 128

static int devid;
static uint32_t last_seqno = 0;
static uint32_t last_seqno_write = 0;

static struct intel_batchbuffer *batch_blt;
static struct intel_batchbuffer *batch_3d;

struct option_struct {
	int rounds;
	int background;
	char cmd[1024];
	int verbose;
	int timeout;
	int dontwrap;
	int prewrap_space;
};

static struct option_struct options;

static void init_buffer(drm_intel_bufmgr *bufmgr,
			struct scratch_buf *buf,
			drm_intel_bo *bo,
			int width, int height)
{
	/* buf->bo = drm_intel_bo_alloc(bufmgr, "", size, 4096); */
	buf->bo = bo;
	buf->size = width * height * 4;
	assert(buf->bo);
	buf->tiling = I915_TILING_NONE;
	buf->data = buf->cpu_mapping = NULL;
	buf->num_tiles = width * height * 4;
	buf->stride = width * 4;
}

static void
set_bo(drm_intel_bo *bo, uint32_t val, int width, int height)
{
	int size = width * height;
	uint32_t *vaddr;

	drm_intel_gem_bo_start_gtt_access(bo, true);
	vaddr = bo->virtual;
	while (size--)
		*vaddr++ = val;
}

static int
cmp_bo(drm_intel_bo *bo, uint32_t val, int width, int height)
{
	int size = width * height;
	uint32_t *vaddr;

	drm_intel_gem_bo_start_gtt_access(bo, false);
	vaddr = bo->virtual;
	while (size--) {
		if (*vaddr++ != val) {
			printf("%d: 0x%x differs from assumed 0x%x\n",
			       width * height - size, *vaddr-1, val);
			return -1;
		}
	}

	return 0;
}

static drm_intel_bo *
create_bo(drm_intel_bufmgr *bufmgr, uint32_t val, int width, int height)
{
	drm_intel_bo *bo;

	bo = drm_intel_bo_alloc(bufmgr, "bo", width * height * 4, 0);
	assert(bo);

	/* gtt map doesn't have a write parameter, so just keep the mapping
	 * around (to avoid the set_domain with the gtt write domain set) and
	 * manually tell the kernel when we start access the gtt. */
	drm_intel_gem_bo_map_gtt(bo);

	set_bo(bo, val, width, height);

	return bo;
}

static void release_bo(drm_intel_bo *bo)
{
	drm_intel_gem_bo_unmap_gtt(bo);
	drm_intel_bo_unreference(bo);
}

static void render_copyfunc(struct scratch_buf *src,
			    struct scratch_buf *dst,
			    int width,
			    int height)
{
	const int src_x = 0, src_y = 0, dst_x = 0, dst_y = 0;
	render_copyfunc_t rendercopy = get_render_copyfunc(devid);

	if (rendercopy) {
		rendercopy(batch_3d,
			   src, src_x, src_y,
			   width, height,
			   dst, dst_x, dst_y);
		intel_batchbuffer_flush(batch_3d);
	} else {
		printf("No render copy found for this gen, test is shallow!\n");
		intel_copy_bo(batch_blt, dst->bo, src->bo, width, height);
		intel_batchbuffer_flush(batch_blt);
	}
}

static int run_sync_test(void)
{
	drm_intel_bufmgr *bufmgr;
	int num_buffers = BUFFERS_TO_SYNC, max;
	drm_intel_bo *src[128], *dst1[128], *dst2[128];
	int width = 128, height = 128;
	int fd;
	int i;
	int r = -1;
	int failed = 0;

	struct scratch_buf s_src[128], s_dst[128];

	fd = drm_open_any();
	assert(fd >= 0);
	devid = intel_get_drm_devid(fd);

	max = gem_aperture_size (fd) / (1024 * 1024) / 2;
	if (num_buffers > max)
		num_buffers = max;

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);
	batch_blt = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));
	assert(batch_blt);
	batch_3d = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));
	assert(batch_3d);

	for (i = 0; i < num_buffers; i++) {
		src[i] = create_bo(bufmgr, i, width, height);
		dst1[i] = create_bo(bufmgr, ~i, width, height);
		dst2[i] = create_bo(bufmgr, ~i, width, height);
		init_buffer(bufmgr, &s_src[i], src[i], width, height);
		init_buffer(bufmgr, &s_dst[i], dst1[i], width, height);
	}

	/* dummy = create_bo(bufmgr, 0, width, height); */

	for (i = 0; i < num_buffers; i++) {
		render_copyfunc(&s_src[i], &s_dst[i], width, height);
		intel_copy_bo(batch_blt, dst2[i], dst1[i], width, height);
	}

	for (i = 0; i < num_buffers; i++) {
		r = cmp_bo(dst2[i], i, width, height);
		if (r) {
			printf("buffer %d differs, seqno_before_test 0x%x, approximated seqno on test fail 0x%x\n",
			       i, last_seqno_write, last_seqno_write + i * 2);
			failed = -1;
		}
	}

	for (i = 0; i < num_buffers; i++) {
		release_bo(src[i]);
		release_bo(dst1[i]);
		release_bo(dst2[i]);
	}

	intel_batchbuffer_free(batch_3d);
	intel_batchbuffer_free(batch_blt);
	drm_intel_bufmgr_destroy(bufmgr);

	close(fd);

	return failed;
}

static int run_cmd(char *s)
{
	int pid;
	int r = -1;
	int status = 0;
	wordexp_t wexp;
	int i;
	r = wordexp(s, &wexp, 0);
	if (r != 0) {
		printf("can't parse %s\n", s);
		return r;
	}

	for(i = 0; i < wexp.we_wordc; i++)
		printf("argv[%d] = %s\n", i, wexp.we_wordv[i]);

	pid = fork();

	if (pid == 0) {
		char path[PATH_MAX];
		char full_path[PATH_MAX];

		if (getcwd(path, PATH_MAX) == NULL)
			perror("getcwd");

		assert(snprintf(full_path, PATH_MAX, "%s/%s", path, wexp.we_wordv[0]) > 0);

		/* if (!options.verbose) {
			close(STDOUT_FILENO);
			close(STDERR_FILENO);
		}
		*/

		r = execv(full_path, wexp.we_wordv);
		if (r == -1)
			perror("execv failed");
	} else {
		int waitcount = options.timeout;

		while(waitcount-- > 0) {
			r = waitpid(pid, &status, WNOHANG);
			if (r == pid) {
				if(WIFEXITED(status)) {
					if (WEXITSTATUS(status))
						fprintf(stderr, "child returned with %d\n", WEXITSTATUS(status));
					return WEXITSTATUS(status);
				}
			} else if (r != 0) {
				perror("waitpid");
				return -errno;
			}

			sleep(1);
		}

		kill(pid, SIGKILL);
		return -ETIMEDOUT;
	}

	return r;
}

static const char *debug_fs_entry = "/sys/kernel/debug/dri/0/i915_next_seqno";

static int read_seqno(uint32_t *seqno)
{
	int fh;
	char buf[32];
	int r;
	char *p;
	unsigned long int tmp;

	fh = open(debug_fs_entry, O_RDWR);
	if (fh == -1) {
		perror("open");
		fprintf(stderr, "no %s found, too old kernel?\n", debug_fs_entry);
		return -errno;
	}

	r = read(fh, buf, sizeof(buf) - 1);
	if (r < 0) {
		perror("read");
		close(fh);
		return -errno;
	}

	close(fh);
	buf[r] = 0;

	p = strstr(buf, "0x");
	if (!p)
		p = buf;

	tmp = strtoul(p, NULL, 0);
	if (tmp == ULONG_MAX) {
		perror("strtoul");
		return -errno;
	}

	*seqno = tmp;

	if (options.verbose)
		printf("seqno read : 0x%x\n", *seqno);

	return 0;
}

static int write_seqno(uint32_t seqno)
{
	int fh;
	char buf[32];
	int r;

	fh = open(debug_fs_entry, O_RDWR);
	if (fh == -1) {
		perror("open");
		return -errno;
	}

	assert(snprintf(buf, sizeof(buf), "0x%x", seqno) > 0);

	r = write(fh, buf, strnlen(buf, sizeof(buf)));
	if (r < 0)
		return r;

	assert(r == strnlen(buf, sizeof(buf)));

	close(fh);

	if (options.verbose)
		printf("seqno write: 0x%x\n", seqno);

	last_seqno_write = seqno;

	return 0;
}

static uint32_t calc_prewrap_val(void)
{
	const int pval = options.prewrap_space - 1;

	return (pval >> 1) + (random() % (pval >> 1));
}

static int run_once(void)
{
	int r;
	uint32_t seqno_before = 0;
	uint32_t seqno_after = 0;
	uint32_t seqno;

	const uint32_t pw_val = calc_prewrap_val();

	r = read_seqno(&seqno_before);
	assert(r == 0);

	if (seqno_before == last_seqno) {
		sleep(2);
		return 0;
	}

	seqno = last_seqno = seqno_before;

	if (seqno < UINT32_MAX - options.prewrap_space) {
		if (seqno < UINT32_MAX/2)
			seqno = UINT32_MAX/2 - options.prewrap_space;
		else
			seqno = UINT32_MAX - pw_val;

		if ((int)(seqno - seqno_before) <= 0)
			return 0;

		if (!options.dontwrap) {
			r = write_seqno(seqno);
			if (r < 0) {
				fprintf(stderr, "write_seqno returned %d\n", r);

				/* We might fail if we are at background and some
				 * operations were done between seqno read and this write
				 */
				if (!options.background)
					return r;
			}
		}
	}

	if (options.background == 0) {
		if (strnlen(options.cmd, sizeof(options.cmd)) > 0) {
			r = run_cmd(options.cmd);
		} else {
			r = run_sync_test();
		}

		if (r != 0) {
			fprintf(stderr, "test returned %d\n", r);
			return -1;
		}
	} else {
		/* Let's wait in background for seqno to increment */
		sleep(2);
	}

	r = read_seqno(&seqno_after);
	assert(r == 0);

	if (seqno_before > seqno_after) {
		if (options.verbose)
			printf("before 0x%x, after 0x%x , diff %d\n",
			       seqno_before, seqno_after, seqno_after - seqno_before);

		return 1;
	}

	return 0;
}

static void print_usage(const char *s)
{
	printf("%s: [OPTION]...\n", s);
	printf("    where options are:\n");
	printf("    -b --background       run in background inducing wraps\n");
	printf("    -c --cmd=cmdstring    use cmdstring to cross wrap\n");
	printf("    -n --rounds=num       run num times across wrap boundary, 0 == forever\n");
	printf("    -t --timeout=sec      set timeout to wait for testrun to sec seconds\n");
	printf("    -d --dontwrap         don't wrap just run the test\n");
	printf("    -p --prewrap=n        set seqno to WRAP - n for each testrun\n");
	exit(-1);
}

static void parse_options(int argc, char **argv)
{
	int c;
	int option_index = 0;
	static struct option long_options[] = {
		{"cmd", required_argument, 0, 'c'},
		{"rounds", required_argument, 0, 'n'},
		{"background", no_argument, 0, 'b'},
		{"timeout", required_argument, 0, 't'},
		{"dontwrap", no_argument, 0, 'd'},
		{"verbose", no_argument, 0, 'v'},
		{"prewrap", required_argument, 0, 'p'},
	};

	strcpy(options.cmd, "");
	options.rounds = 5;
	options.background = 0;
	options.dontwrap = 0;
	options.timeout = 20;
	options.verbose = 0;
	options.prewrap_space = BUFFERS_TO_SYNC/2;

	while((c = getopt_long(argc, argv, "c:n:bvt:dp:",
			       long_options, &option_index)) != -1) {
		switch(c) {
		case 'b':
			options.background = 1;
			printf("running in background inducing wraps\n");
			break;
		case 'd':
			options.dontwrap = 1;
			printf("won't wrap after testruns\n");
			break;
		case 'n':
			options.rounds = atoi(optarg);
			printf("running %d rounds\n", options.rounds);
			break;
		case 'c':
			strncpy(options.cmd, optarg, sizeof(options.cmd) - 1);
			options.cmd[sizeof(options.cmd) - 1] = 0;
			printf("cmd set to %s\n", options.cmd);
			break;
		case 't':
			options.timeout = atoi(optarg);
			if (options.timeout == 0)
				options.timeout = 10;
			printf("setting timeout to %d seconds\n", options.timeout);
			break;
		case 'v':
			options.verbose = 1;
			break;
		case 'p':
			options.prewrap_space = atoi(optarg);
			if (options.prewrap_space == 0)
				options.prewrap_space = 1;
			printf("prewrap set to %d (0x%x)\n",
			       options.prewrap_space, UINT32_MAX -
			       options.prewrap_space);
			break;
		default:
			printf("unkown command options\n");
			print_usage(argv[0]);
			break;
		}
	}

	if (optind < argc) {
		printf("unkown command options\n");
		print_usage(argv[0]);
	}
}

int main(int argc, char **argv)
{
	int wcount = 0;
	int r = -1;

	parse_options(argc, argv);

	srandom(time(NULL));

	while(options.rounds == 0 || wcount < options.rounds) {
		r = run_once();
		if (r < 0) {
			if (options.verbose) fprintf(stderr,
						     "run once returned %d\n",
						     r);
			return r;
		}

		if (options.dontwrap)
			wcount++;
		else
			wcount += r;

		if (options.verbose) {
			printf("%s done: %d\n",
			       options.dontwrap ? "tests" : "wraps", wcount);
			fflush(stdout);
		}
	}

	if (options.rounds == wcount) {
		if (options.verbose)
			printf("done %d wraps successfully\n", wcount);
		return 0;
	}

	return r;
}

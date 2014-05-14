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
 * boundary.
 */

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <limits.h>
#include <wordexp.h>
#include <getopt.h>
#include <signal.h>
#include <errno.h>

#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "igt_core.h"
#include "igt_aux.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "intel_io.h"
#include "intel_chipset.h"

static int devid;
static int card_index = 0;
static uint32_t last_seqno = 0;

static struct intel_batchbuffer *batch_blt;
static struct intel_batchbuffer *batch_3d;

struct option_struct {
	int rounds;
	int background;
	char cmd[1024];
	int timeout;
	int dontwrap;
	int prewrap_space;
	int random;
	int buffers;
};

static struct option_struct options;

static void init_buffer(drm_intel_bufmgr *bufmgr,
			struct igt_buf *buf,
			drm_intel_bo *bo,
			int width, int height)
{
	/* buf->bo = drm_intel_bo_alloc(bufmgr, "", size, 4096); */
	buf->bo = bo;
	buf->size = width * height * 4;
	igt_assert(buf->bo);
	buf->tiling = I915_TILING_NONE;
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

static void
cmp_bo(drm_intel_bo *bo, uint32_t val, int width, int height)
{
	int size = width * height;
	uint32_t *vaddr;

	drm_intel_gem_bo_start_gtt_access(bo, false);
	vaddr = bo->virtual;
	while (size--) {
		igt_assert_f(*vaddr++ == val,
			     "%d: 0x%x differs from assumed 0x%x\n"
			     "seqno_before_test 0x%x, "
			     " approximated seqno on test fail 0x%x\n",
			     width * height - size, *vaddr-1, val,
			     last_seqno, last_seqno + val * 2);
	}
}

static drm_intel_bo *
create_bo(drm_intel_bufmgr *bufmgr, uint32_t val, int width, int height)
{
	drm_intel_bo *bo;

	bo = drm_intel_bo_alloc(bufmgr, "bo", width * height * 4, 0);
	igt_assert(bo);

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

static void render_copyfunc(struct igt_buf *src,
			    struct igt_buf *dst,
			    int width,
			    int height)
{
	const int src_x = 0, src_y = 0, dst_x = 0, dst_y = 0;
	igt_render_copyfunc_t rendercopy = igt_get_render_copyfunc(devid);
	static int warned = 0;

	if (rendercopy) {
		rendercopy(batch_3d, NULL,
			   src, src_x, src_y,
			   width, height,
			   dst, dst_x, dst_y);
		intel_batchbuffer_flush(batch_3d);
	} else {
		if (!warned) {
			printf("No render copy found for this gen, "
			       "test is shallow!\n");
			warned = 1;
		}
		igt_assert(dst->bo);
		igt_assert(src->bo);
		intel_copy_bo(batch_blt, dst->bo, src->bo, width*height*4);
		intel_batchbuffer_flush(batch_blt);
	}
}

static void exchange_uint(void *array, unsigned i, unsigned j)
{
	unsigned *i_arr = array;
	unsigned i_tmp;

	i_tmp = i_arr[i];
	i_arr[i] = i_arr[j];
	i_arr[j] = i_tmp;
}

static void run_sync_test(int num_buffers, bool verify)
{
	drm_intel_bufmgr *bufmgr;
	int max;
	drm_intel_bo **src, **dst1, **dst2;
	int width = 128, height = 128;
	int fd;
	int i;
	unsigned int *p_dst1, *p_dst2;
	struct igt_buf *s_src, *s_dst;

	fd = drm_open_any();
	igt_assert(fd >= 0);

	gem_quiescent_gpu(fd);

	devid = intel_get_drm_devid(fd);

	max = gem_aperture_size (fd) / (1024 * 1024) / 2;
	if (num_buffers > max)
		num_buffers = max;

	bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	drm_intel_bufmgr_gem_enable_reuse(bufmgr);
	batch_blt = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));
	igt_assert(batch_blt);
	batch_3d = intel_batchbuffer_alloc(bufmgr, intel_get_drm_devid(fd));
	igt_assert(batch_3d);

	src = malloc(num_buffers * sizeof(*src));
	igt_assert(src);

	dst1 = malloc(num_buffers * sizeof(*dst1));
	igt_assert(dst1);

	dst2 = malloc(num_buffers * sizeof(*dst2));
	igt_assert(dst2);

	s_src = malloc(num_buffers * sizeof(*s_src));
	igt_assert(s_src);

	s_dst = malloc(num_buffers * sizeof(*s_dst));
	igt_assert(s_dst);

	p_dst1 = malloc(num_buffers * sizeof(unsigned int));
	igt_assert(p_dst1);

	p_dst2 = malloc(num_buffers * sizeof(unsigned int));
	igt_assert(p_dst2);

	for (i = 0; i < num_buffers; i++) {
		p_dst1[i] = p_dst2[i] = i;
		src[i] = create_bo(bufmgr, i, width, height);
		igt_assert(src[i]);
		dst1[i] = create_bo(bufmgr, ~i, width, height);
		igt_assert(dst1[i]);
		dst2[i] = create_bo(bufmgr, ~i, width, height);
		igt_assert(dst2[i]);
		init_buffer(bufmgr, &s_src[i], src[i], width, height);
		init_buffer(bufmgr, &s_dst[i], dst1[i], width, height);
	}

	igt_permute_array(p_dst1, num_buffers, exchange_uint);
	igt_permute_array(p_dst2, num_buffers, exchange_uint);

	for (i = 0; i < num_buffers; i++)
		render_copyfunc(&s_src[i], &s_dst[p_dst1[i]], width, height);

	/* Only sync between buffers if this is actual test run and
	 * not a seqno filler */
	if (verify) {
		for (i = 0; i < num_buffers; i++)
			intel_copy_bo(batch_blt, dst2[p_dst2[i]], dst1[p_dst1[i]],
				      width*height*4);

		for (i = 0; i < num_buffers; i++) {
			cmp_bo(dst2[p_dst2[i]], i, width, height);
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

	free(p_dst1);
	free(p_dst2);
	free(s_dst);
	free(s_src);
	free(dst2);
	free(dst1);
	free(src);

	gem_quiescent_gpu(fd);

	close(fd);
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

		igt_assert(snprintf(full_path, PATH_MAX, "%s/%s", path, wexp.we_wordv[0]) > 0);

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
						fprintf(stderr,
						    "child returned with %d\n",
							WEXITSTATUS(status));
					return WEXITSTATUS(status);
				}
			} else if (r != 0) {
				perror("waitpid");
				return -errno;
			}

			sleep(3);
		}

		kill(pid, SIGKILL);
		return -ETIMEDOUT;
	}

	return r;
}

static const char *dfs_base = "/sys/kernel/debug/dri";
static const char *dfs_entry = "i915_next_seqno";

static int dfs_open(int mode)
{
	char fname[FILENAME_MAX];
	int fh;

	snprintf(fname, FILENAME_MAX, "%s/%i/%s",
		 dfs_base, card_index, dfs_entry);

	fh = open(fname, mode);
	igt_require(fh >= 0);

	return fh;
}

static int __read_seqno(uint32_t *seqno)
{
	int fh;
	char buf[32];
	int r;
	char *p;
	unsigned long int tmp;

	fh = dfs_open(O_RDONLY);

	r = read(fh, buf, sizeof(buf) - 1);
	close(fh);
	if (r < 0) {
		perror("read");
		return -errno;
	}

	buf[r] = 0;

	p = strstr(buf, "0x");
	if (!p)
		p = buf;

	errno = 0;
	tmp = strtoul(p, NULL, 0);
	if (tmp == ULONG_MAX && errno) {
		perror("strtoul");
		return -errno;
	}

	*seqno = tmp;

	igt_debug("next_seqno: 0x%x\n", *seqno);

	return 0;
}

static int read_seqno(void)
{
	uint32_t seqno = 0;
	int r;
	int wrap = 0;

	r = __read_seqno(&seqno);
	igt_assert(r == 0);

	if (last_seqno > seqno)
		wrap++;

	last_seqno = seqno;

	return wrap;
}

static int write_seqno(uint32_t seqno)
{
	int fh;
	char buf[32];
	int r;
	uint32_t rb;

	if (options.dontwrap)
		return 0;

	fh = dfs_open(O_RDWR);
	igt_assert(snprintf(buf, sizeof(buf), "0x%x", seqno) > 0);

	r = write(fh, buf, strnlen(buf, sizeof(buf)));
	close(fh);
	if (r < 0)
		return r;

	igt_assert(r == strnlen(buf, sizeof(buf)));

	last_seqno = seqno;

	igt_debug("next_seqno set to: 0x%x\n", seqno);

	r = __read_seqno(&rb);
	if (r < 0)
		return r;

	if (rb != seqno) {
		printf("seqno readback differs rb:0x%x vs w:0x%x\n", rb, seqno);
		return -1;
	}

	return 0;
}

static uint32_t calc_prewrap_val(void)
{
	const int pval = options.prewrap_space;

	if (options.random == 0)
		return pval;

	if (pval == 0)
		return 0;

	return (random() % pval);
}

static void run_test(void)
{
	if (strnlen(options.cmd, sizeof(options.cmd)) > 0)
		igt_assert(run_cmd(options.cmd) == 0);
	else
		run_sync_test(options.buffers, true);
}

static void preset_run_once(void)
{
	igt_assert(write_seqno(1) == 0);
	run_test();

	igt_assert(write_seqno(0x7fffffff) == 0);
	run_test();

	igt_assert(write_seqno(0xffffffff) == 0);
	run_test();

	igt_assert(write_seqno(0xfffffff0) == 0);
	run_test();
}

static void random_run_once(void)
{
	uint32_t val;

	do {
		val = random() % UINT32_MAX;
		if (RAND_MAX < UINT32_MAX)
			val += random();
	} while (val == 0);

	igt_assert(write_seqno(val) == 0);
	run_test();
}

static void wrap_run_once(void)
{
	const uint32_t pw_val = calc_prewrap_val();

	igt_assert(write_seqno(UINT32_MAX - pw_val) == 0);

	while(!read_seqno())
		run_test();
}

static void background_run_once(void)
{
	const uint32_t pw_val = calc_prewrap_val();

	igt_assert(write_seqno(UINT32_MAX - pw_val) == 0);

	while(!read_seqno())
		sleep(3);
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
	printf("    -r --norandom         dont randomize prewrap space\n");
	printf("    -i --buffers          number of buffers to copy\n");
	igt_fail(-1);
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
		{"prewrap", required_argument, 0, 'p'},
		{"norandom", no_argument, 0, 'r'},
		{"buffers", required_argument, 0, 'i'},
	};

	strcpy(options.cmd, "");
	options.rounds = SLOW_QUICK(50, 2);
	options.background = 0;
	options.dontwrap = 0;
	options.timeout = 20;
	options.random = 1;
	options.prewrap_space = 21;
	options.buffers = 10;

	while((c = getopt_long(argc, argv, "c:n:bvt:dp:ri:",
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
		case 'i':
			options.buffers = atoi(optarg);
			printf("buffers %d\n", options.buffers);
			break;
		case 't':
			options.timeout = atoi(optarg);
			if (options.timeout == 0)
				options.timeout = 10;
			printf("setting timeout to %d seconds\n",
			       options.timeout);
			break;
		case 'r':
			options.random = 0;
			break;
		case 'p':
			options.prewrap_space = atoi(optarg);
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

	igt_simple_init();

	parse_options(argc, argv);

	card_index = drm_get_card();

	srandom(time(NULL));

	while(options.rounds == 0 || wcount < options.rounds) {
		if (options.background) {
			background_run_once();
		} else {
			preset_run_once();
			random_run_once();
			wrap_run_once();
		}

		wcount++;

		igt_debug("%s done: %d\n",
			  options.dontwrap ? "tests" : "wraps", wcount);
	}

	if (options.rounds == wcount) {
		igt_debug("done %d wraps successfully\n", wcount);
		return 0;
	}

	return r;
}

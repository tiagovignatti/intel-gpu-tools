/*
 * Copyright © 2012 Intel Corporation
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
 *    Ben Widawsky <ben@bwidawsk.net>
 *    Jeff McGee <jeff.mcgee@intel.com>
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <fcntl.h>
#include <signal.h>
#include "drmtest.h"
#include "intel_gpu_tools.h"
#include "intel_bufmgr.h"
#include "intel_batchbuffer.h"
#include "igt_debugfs.h"

static bool verbose = false;

static int drm_fd;

static const char sysfs_base_path[] = "/sys/class/drm/card%d/gt_%s_freq_mhz";
enum {
	CUR,
	MIN,
	MAX,
	RP0,
	RP1,
	RPn,
	NUMFREQ
};

static int origfreqs[NUMFREQ];

struct junk {
	const char *name;
	const char *mode;
	FILE *filp;
} stuff[] = {
	{ "cur", "r", NULL }, { "min", "rb+", NULL }, { "max", "rb+", NULL }, { "RP0", "r", NULL }, { "RP1", "r", NULL }, { "RPn", "r", NULL }, { NULL, NULL, NULL }
};

static igt_debugfs_t dfs;

static int readval(FILE *filp)
{
	int val;
	int scanned;

	rewind(filp);
	scanned = fscanf(filp, "%d", &val);
	igt_assert(scanned == 1);

	return val;
}

static void read_freqs(int *freqs)
{
	int i;

	for (i = 0; i < NUMFREQ; i++)
		freqs[i] = readval(stuff[i].filp);
}

static int do_writeval(FILE *filp, int val, int lerrno)
{
	int ret, orig;

	orig = readval(filp);
	rewind(filp);
	ret = fprintf(filp, "%d", val);

	if (lerrno) {
		/* Expecting specific error */
		igt_assert(ret == EOF && errno == lerrno);
		igt_assert(readval(filp) == orig);
	} else {
		/* Expecting no error */
		igt_assert(ret != EOF);
		igt_assert(readval(filp) == val);
	}

	return ret;
}
#define writeval(filp, val) do_writeval(filp, val, 0)
#define writeval_inval(filp, val) do_writeval(filp, val, EINVAL)

static void checkit(const int *freqs)
{
	igt_assert(freqs[MIN] <= freqs[MAX]);
	igt_assert(freqs[CUR] <= freqs[MAX]);
	igt_assert(freqs[MIN] <= freqs[CUR]);
	igt_assert(freqs[RPn] <= freqs[MIN]);
	igt_assert(freqs[MAX] <= freqs[RP0]);
	igt_assert(freqs[RP1] <= freqs[RP0]);
	igt_assert(freqs[RPn] <= freqs[RP1]);
	igt_assert(freqs[RP0] != 0);
	igt_assert(freqs[RP1] != 0);
}

static void matchit(const int *freqs1, const int *freqs2)
{
	igt_assert(freqs1[CUR] == freqs2[CUR]);
	igt_assert(freqs1[MIN] == freqs2[MIN]);
	igt_assert(freqs1[MAX] == freqs2[MAX]);
	igt_assert(freqs1[RP0] == freqs2[RP0]);
	igt_assert(freqs1[RP1] == freqs2[RP1]);
	igt_assert(freqs1[RPn] == freqs2[RPn]);
}

static void dumpit(const int *freqs)
{
	int i;

	printf("gt freq (MHz):");
	for (i = 0; i < NUMFREQ; i++)
		printf("  %s=%d", stuff[i].name, freqs[i]);

	printf("\n");
}
#define dump(x) if (verbose) dumpit(x)
#define log(...) if (verbose) printf(__VA_ARGS__)

enum load {
	LOW,
	HIGH
};

static struct load_helper {
	int devid;
	int has_ppgtt;
	drm_intel_bufmgr *bufmgr;
	struct intel_batchbuffer *batch;
	drm_intel_bo *target_buffer;
	bool ready;
	enum load load;
	bool exit;
	struct igt_helper_process igt_proc;
} lh;

static void load_helper_signal_handler(int sig)
{
	if (sig == SIGUSR2)
		lh.load = lh.load == LOW ? HIGH : LOW;
	else
		lh.exit = true;
}

static void emit_store_dword_imm(uint32_t val)
{
	int cmd;
	struct intel_batchbuffer *batch = lh.batch;

	cmd = MI_STORE_DWORD_IMM;
	if (!lh.has_ppgtt)
		cmd |= MI_MEM_VIRTUAL;

	if (intel_gen(lh.devid) >= 8) {
		BEGIN_BATCH(4);
		OUT_BATCH(cmd);
		OUT_RELOC(lh.target_buffer, I915_GEM_DOMAIN_INSTRUCTION,
			  I915_GEM_DOMAIN_INSTRUCTION, 0);
		OUT_BATCH(0);
		OUT_BATCH(val);
		ADVANCE_BATCH();
	} else {
		BEGIN_BATCH(4);
		OUT_BATCH(cmd);
		OUT_BATCH(0); /* reserved */
		OUT_RELOC(lh.target_buffer, I915_GEM_DOMAIN_INSTRUCTION,
			  I915_GEM_DOMAIN_INSTRUCTION, 0);
		OUT_BATCH(val);
		ADVANCE_BATCH();
	}
}

#define LOAD_HELPER_PAUSE_USEC 500
static void load_helper_run(enum load load)
{
	assert(!lh.igt_proc.running);

	igt_require(lh.ready == true);

	lh.load = load;

	igt_fork_helper(&lh.igt_proc) {
		uint32_t val = 0;

		signal(SIGUSR1, load_helper_signal_handler);
		signal(SIGUSR2, load_helper_signal_handler);

		while (!lh.exit) {
			emit_store_dword_imm(val);
			intel_batchbuffer_flush_on_ring(lh.batch, 0);
			val++;

			/* Lower the load by pausing after every submitted
			 * write. */
			if (lh.load == LOW)
				usleep(LOAD_HELPER_PAUSE_USEC);
		}

		/* Map buffer to stall for write completion */
		drm_intel_bo_map(lh.target_buffer, 0);
		drm_intel_bo_unmap(lh.target_buffer);

		log("load helper sent %u dword writes\n", val);
	}
}

static void load_helper_set_load(enum load load)
{
	assert(lh.igt_proc.running);

	if (lh.load == load)
		return;

	lh.load = load;
	kill(lh.igt_proc.pid, SIGUSR2);
}

static void load_helper_stop(void)
{
	assert(lh.igt_proc.running);
	kill(lh.igt_proc.pid, SIGUSR1);
	igt_wait_helper(&lh.igt_proc);
}

/* The load helper resource is used by only some subtests. We attempt to
 * initialize in igt_fixture but do our igt_require check only if a
 * subtest attempts to run it */
static void load_helper_init(void)
{
	lh.devid = intel_get_drm_devid(drm_fd);
	lh.has_ppgtt = gem_uses_aliasing_ppgtt(drm_fd);

	/* MI_STORE_DATA can only use GTT address on gen4+/g33 and needs
	 * snoopable mem on pre-gen6. */
	if (intel_gen(lh.devid) < 6) {
		log("load helper init failed: pre-gen6 not supported\n");
		return;
	}

	lh.bufmgr = drm_intel_bufmgr_gem_init(drm_fd, 4096);
	if (!lh.bufmgr) {
		log("load helper init failed: buffer manager init\n");
		return;
	}
	drm_intel_bufmgr_gem_enable_reuse(lh.bufmgr);

	lh.batch = intel_batchbuffer_alloc(lh.bufmgr, lh.devid);
	if (!lh.batch) {
		log("load helper init failed: batch buffer alloc\n");
		return;
	}

	lh.target_buffer = drm_intel_bo_alloc(lh.bufmgr, "target bo",
					      4096, 4096);
	if (!lh.target_buffer) {
		log("load helper init failed: target buffer alloc\n");
		return;
	}

	lh.ready = true;
}

static void load_helper_deinit(void)
{
	if (lh.igt_proc.running)
		load_helper_stop();

	if (lh.target_buffer)
		drm_intel_bo_unreference(lh.target_buffer);

	if (lh.batch)
		intel_batchbuffer_free(lh.batch);

	if (lh.bufmgr)
		drm_intel_bufmgr_destroy(lh.bufmgr);
}

static void stop_rings(void)
{
	int fd;
	static const char data[] = "0xf";

	fd = igt_debugfs_open(&dfs, "i915_ring_stop", O_WRONLY);
	igt_assert(fd >= 0);

	log("injecting ring stop\n");
	igt_assert(write(fd, data, sizeof(data)) == sizeof(data));

	close(fd);
}

static bool rings_stopped(void)
{
	int fd;
	static char buf[128];
	unsigned long long val;

	fd = igt_debugfs_open(&dfs, "i915_ring_stop", O_RDONLY);
	igt_assert(fd >= 0);

	igt_assert(read(fd, buf, sizeof(buf)) > 0);
	close(fd);

	sscanf(buf, "%llx", &val);

	return (bool)val;
}

static void min_max_config(void (*check)(void))
{
	int fmid = (origfreqs[RPn] + origfreqs[RP0]) / 2;

	/* hw (and so kernel) currently rounds to 50 MHz ... */
	fmid = fmid / 50 * 50;

	log("\nCheck original min and max...\n");
	check();

	log("\nSet min=RPn and max=RP0...\n");
	writeval(stuff[MIN].filp, origfreqs[RPn]);
	writeval(stuff[MAX].filp, origfreqs[RP0]);
	check();

	log("\nIncrease min to midpoint...\n");
	writeval(stuff[MIN].filp, fmid);
	check();

	log("\nIncrease min to RP0...\n");
	writeval(stuff[MIN].filp, origfreqs[RP0]);
	check();

	log("\nIncrease min above RP0 (invalid)...\n");
	writeval_inval(stuff[MIN].filp, origfreqs[RP0] + 1000);
	check();

	log("\nDecrease max to RPn (invalid)...\n");
	writeval_inval(stuff[MAX].filp, origfreqs[RPn]);
	check();

	log("\nDecrease min to midpoint...\n");
	writeval(stuff[MIN].filp, fmid);
	check();

	log("\nDecrease min to RPn...\n");
	writeval(stuff[MIN].filp, origfreqs[RPn]);
	check();

	log("\nDecrease min below RPn (invalid)...\n");
	writeval_inval(stuff[MIN].filp, 0);
	check();

	log("\nDecrease max to midpoint...\n");
	writeval(stuff[MAX].filp, fmid);
	check();

	log("\nDecrease max to RPn...\n");
	writeval(stuff[MAX].filp, origfreqs[RPn]);
	check();

	log("\nDecrease max below RPn (invalid)...\n");
	writeval_inval(stuff[MAX].filp, 0);
	check();

	log("\nIncrease min to RP0 (invalid)...\n");
	writeval_inval(stuff[MIN].filp, origfreqs[RP0]);
	check();

	log("\nIncrease max to midpoint...\n");
	writeval(stuff[MAX].filp, fmid);
	check();

	log("\nIncrease max to RP0...\n");
	writeval(stuff[MAX].filp, origfreqs[RP0]);
	check();

	log("\nIncrease max above RP0 (invalid)...\n");
	writeval_inval(stuff[MAX].filp, origfreqs[RP0] + 1000);
	check();

	writeval(stuff[MIN].filp, origfreqs[MIN]);
	writeval(stuff[MAX].filp, origfreqs[MAX]);
}

static void basic_check(void)
{
	int freqs[NUMFREQ];

	read_freqs(freqs);
	dump(freqs);
	checkit(freqs);
}

#define IDLE_WAIT_TIMESTEP_MSEC 100
#define IDLE_WAIT_TIMEOUT_MSEC 3000
static void idle_check(void)
{
	int freqs[NUMFREQ];
	int wait = 0;

	/* Monitor frequencies until cur settles down to min, which should
	 * happen within the allotted time */
	do {
		read_freqs(freqs);
		dump(freqs);
		checkit(freqs);
		if (freqs[CUR] == freqs[MIN])
			break;
		usleep(1000 * IDLE_WAIT_TIMESTEP_MSEC);
		wait += IDLE_WAIT_TIMESTEP_MSEC;
	} while (wait < IDLE_WAIT_TIMEOUT_MSEC);

	igt_assert(freqs[CUR] == freqs[MIN]);
	log("Required %d msec to reach cur=min\n", wait);
}

#define LOADED_WAIT_TIMESTEP_MSEC 100
#define LOADED_WAIT_TIMEOUT_MSEC 3000
static void loaded_check(void)
{
	int freqs[NUMFREQ];
	int wait = 0;

	/* Monitor frequencies until cur increases to max, which should
	 * happen within the allotted time */
	do {
		read_freqs(freqs);
		dump(freqs);
		checkit(freqs);
		if (freqs[CUR] == freqs[MAX])
			break;
		usleep(1000 * LOADED_WAIT_TIMESTEP_MSEC);
		wait += LOADED_WAIT_TIMESTEP_MSEC;
	} while (wait < LOADED_WAIT_TIMEOUT_MSEC);

	igt_assert(freqs[CUR] == freqs[MAX]);
	log("Required %d msec to reach cur=max\n", wait);
}

#define STABILIZE_WAIT_TIMESTEP_MSEC 100
#define STABILIZE_WAIT_TIMEOUT_MSEC 2000
static void stabilize_check(int *freqs)
{
	int wait = 0;

	do {
		read_freqs(freqs);
		dump(freqs);
		usleep(1000 * STABILIZE_WAIT_TIMESTEP_MSEC);
		wait += STABILIZE_WAIT_TIMESTEP_MSEC;
	} while (wait < STABILIZE_WAIT_TIMEOUT_MSEC);

	log("Waited %d msec to stabilize cur\n", wait);
}

static void reset(void)
{
	int pre_freqs[NUMFREQ];
	int post_freqs[NUMFREQ];

	log("Apply low load...\n");
	load_helper_run(LOW);
	stabilize_check(pre_freqs);

	log("Stop rings...\n");
	stop_rings();
	while (rings_stopped())
		usleep(1000 * 100);
	log("Ring stop cleared\n");

	log("Apply high load...\n");
	load_helper_set_load(HIGH);
	loaded_check();

	log("Apply low load...\n");
	load_helper_set_load(LOW);
	stabilize_check(post_freqs);
	matchit(pre_freqs, post_freqs);

	log("Apply high load...\n");
	load_helper_set_load(HIGH);
	loaded_check();

	log("Removing load...\n");
	load_helper_stop();
	idle_check();
}

static void pm_rps_exit_handler(int sig)
{
	if (origfreqs[MIN] > readval(stuff[MAX].filp)) {
		writeval(stuff[MAX].filp, origfreqs[MAX]);
		writeval(stuff[MIN].filp, origfreqs[MIN]);
	} else {
		writeval(stuff[MIN].filp, origfreqs[MIN]);
		writeval(stuff[MAX].filp, origfreqs[MAX]);
	}

	load_helper_deinit();
	close(drm_fd);
}

static int opt_handler(int opt, int opt_index)
{
	switch (opt) {
	case 'v':
		verbose = true;
		break;
	default:
		assert(0);
	}

	return 0;
}

/* Mod of igt_subtest_init that adds our extra options */
static void subtest_init(int argc, char **argv)
{
	struct option long_opts[] = {
		{"verbose", 0, 0, 'v'}
	};
	const char *help_str = "  -v, --verbose";
	int ret;

	ret = igt_subtest_init_parse_opts(argc, argv, "v", long_opts,
					  help_str, opt_handler);

	if (ret < 0)
		/* exit with no error for -h/--help */
		exit(ret == -1 ? 0 : ret);
}

int main(int argc, char **argv)
{
	subtest_init(argc, argv);

	igt_skip_on_simulation();

	igt_fixture {
		const int device = drm_get_card();
		struct junk *junk = stuff;
		int ret;

		/* Use drm_open_any to verify device existence */
		drm_fd = drm_open_any();

		do {
			int val = -1;
			char *path;
			ret = asprintf(&path, sysfs_base_path, device, junk->name);
			igt_assert(ret != -1);
			junk->filp = fopen(path, junk->mode);
			igt_require(junk->filp);
			setbuf(junk->filp, NULL);

			val = readval(junk->filp);
			igt_assert(val >= 0);
			junk++;
		} while(junk->name != NULL);

		read_freqs(origfreqs);

		igt_install_exit_handler(pm_rps_exit_handler);

		load_helper_init();

		igt_debugfs_init(&dfs);
	}

	igt_subtest("basic-api")
		min_max_config(basic_check);

	igt_subtest("min-max-config-idle")
		min_max_config(idle_check);

	igt_subtest("min-max-config-loaded") {
		load_helper_run(HIGH);
		min_max_config(loaded_check);
		load_helper_stop();
	}

	igt_subtest("reset")
		reset();

	igt_exit();
}

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
#include "drmtest.h"

static bool verbose = false;

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

static void setfreq(int val)
{
	if (val > readval(stuff[MAX].filp)) {
		writeval(stuff[MAX].filp, val);
		writeval(stuff[MIN].filp, val);
	} else {
		writeval(stuff[MIN].filp, val);
		writeval(stuff[MAX].filp, val);
	}
}

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

static void dumpit(const int *freqs)
{
	int i;

	for (i = 0; i < NUMFREQ; i++)
		printf("gt frequency %s (MHz):  %d\n", stuff[i].name, freqs[i]);

	printf("\n");
}
#define dump(x) if (verbose) dumpit(x)
#define log(...) if (verbose) printf(__VA_ARGS__)

static void min_max_config(void (*check)(void))
{
	int fmid = (origfreqs[RPn] + origfreqs[RP0]) / 2;

	log("Check original min and max...\n");
	check();

	log("Set min=RPn and max=RP0...\n");
	writeval(stuff[MIN].filp, origfreqs[RPn]);
	writeval(stuff[MAX].filp, origfreqs[RP0]);
	check();

	log("Increase min to midpoint...\n");
	writeval(stuff[MIN].filp, fmid);
	check();

	log("Increase min to RP0...\n");
	writeval(stuff[MIN].filp, origfreqs[RP0]);
	check();

	log("Increase min above RP0 (invalid)...\n");
	writeval_inval(stuff[MIN].filp, origfreqs[RP0] + 1000);
	check();

	log("Decrease max to RPn (invalid)...\n");
	writeval_inval(stuff[MAX].filp, origfreqs[RPn]);
	check();

	log("Decrease min to midpoint...\n");
	writeval(stuff[MIN].filp, fmid);
	check();

	log("Decrease min to RPn...\n");
	writeval(stuff[MIN].filp, origfreqs[RPn]);
	check();

	log("Decrease min below RPn (invalid)...\n");
	writeval_inval(stuff[MIN].filp, 0);
	check();

	log("Decrease max to midpoint...\n");
	writeval(stuff[MAX].filp, fmid);
	check();

	log("Decrease max to RPn...\n");
	writeval(stuff[MAX].filp, origfreqs[RPn]);
	check();

	log("Decrease max below RPn (invalid)...\n");
	writeval_inval(stuff[MAX].filp, 0);
	check();

	log("Increase min to RP0 (invalid)...\n");
	writeval_inval(stuff[MIN].filp, origfreqs[RP0]);
	check();

	log("Increase max to midpoint...\n");
	writeval(stuff[MAX].filp, fmid);
	check();

	log("Increase max to RP0...\n");
	writeval(stuff[MAX].filp, origfreqs[RP0]);
	check();

	log("Increase max above RP0 (invalid)...\n");
	writeval_inval(stuff[MAX].filp, origfreqs[RP0] + 1000);
	check();
}

static void idle_check(void)
{
	int freqs[NUMFREQ];

	read_freqs(freqs);
	dump(freqs);
	checkit(freqs);
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
		int fd, ret;

		/* Use drm_open_any to verify device existence */
		fd = drm_open_any();
		close(fd);

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
	}

	igt_subtest("min-max-config-at-idle")
		min_max_config(idle_check);

	igt_exit();
}

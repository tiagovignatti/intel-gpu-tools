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
static int origmin, origmax;

static const char sysfs_base_path[] = "/sys/class/drm/card%d/gt_%s_freq_mhz";
enum {
	CUR,
	MIN,
	MAX,
	RP0,
	RP1,
	RPn
};

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

#define fcur (readval(stuff[CUR].filp))
#define fmin (readval(stuff[MIN].filp))
#define fmax (readval(stuff[MAX].filp))
#define frp0 (readval(stuff[RP0].filp))
#define frp1 (readval(stuff[RP1].filp))
#define frpn (readval(stuff[RPn].filp))

static void setfreq(int val)
{
	if (val > fmax) {
		writeval(stuff[MAX].filp, val);
		writeval(stuff[MIN].filp, val);
	} else {
		writeval(stuff[MIN].filp, val);
		writeval(stuff[MAX].filp, val);
	}
}

static void checkit(void)
{
	igt_assert(fmin <= fmax);
	igt_assert(fcur <= fmax);
	igt_assert(fmin <= fcur);
	igt_assert(frpn <= fmin);
	igt_assert(fmax <= frp0);
	igt_assert(frp1 <= frp0);
	igt_assert(frpn <= frp1);
	igt_assert(frp0 != 0);
	igt_assert(frp1 != 0);
}

static void dumpit(void)
{
	struct junk *junk = stuff;
	do {
		printf("gt frequency %s (MHz):  %d\n", junk->name, readval(junk->filp));
		junk++;
	} while(junk->name != NULL);

	printf("\n");
}
#define dump() if (verbose) dumpit()
#define log(...) if (verbose) printf(__VA_ARGS__)

static void min_max_config(void (*check)(void))
{
	int fmid = (frpn + frp0) / 2;

	log("Check original min and max...\n");
	check();

	log("Set min=RPn and max=RP0...\n");
	writeval(stuff[MIN].filp, frpn);
	writeval(stuff[MAX].filp, frp0);
	check();

	log("Increase min to midpoint...\n");
	writeval(stuff[MIN].filp, fmid);
	check();

	log("Increase min to RP0...\n");
	writeval(stuff[MIN].filp, frp0);
	check();

	log("Increase min above RP0 (invalid)...\n");
	writeval_inval(stuff[MIN].filp, frp0 + 1000);
	check();

	log("Decrease max to RPn (invalid)...\n");
	writeval_inval(stuff[MAX].filp, frpn);
	check();

	log("Decrease min to midpoint...\n");
	writeval(stuff[MIN].filp, fmid);
	check();

	log("Decrease min to RPn...\n");
	writeval(stuff[MIN].filp, frpn);
	check();

	log("Decrease min below RPn (invalid)...\n");
	writeval_inval(stuff[MIN].filp, 0);
	check();

	log("Decrease max to midpoint...\n");
	writeval(stuff[MAX].filp, fmid);
	check();

	log("Decrease max to RPn...\n");
	writeval(stuff[MAX].filp, frpn);
	check();

	log("Decrease max below RPn (invalid)...\n");
	writeval_inval(stuff[MAX].filp, 0);
	check();

	log("Increase min to RP0 (invalid)...\n");
	writeval_inval(stuff[MIN].filp, frp0);
	check();

	log("Increase max to midpoint...\n");
	writeval(stuff[MAX].filp, fmid);
	check();

	log("Increase max to RP0...\n");
	writeval(stuff[MAX].filp, frp0);
	check();

	log("Increase max above RP0 (invalid)...\n");
	writeval_inval(stuff[MAX].filp, frp0 + 1000);
	check();
}

static void idle_check(void)
{
	dump();
	checkit();
}

static void pm_rps_exit_handler(int sig)
{
	if (origmin > fmax) {
		writeval(stuff[MAX].filp, origmax);
		writeval(stuff[MIN].filp, origmin);
	} else {
		writeval(stuff[MIN].filp, origmin);
		writeval(stuff[MAX].filp, origmax);
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

		origmin = fmin;
		origmax = fmax;

		igt_install_exit_handler(pm_rps_exit_handler);
	}

	igt_subtest("min-max-config-at-idle")
		min_max_config(idle_check);

	igt_exit();
}

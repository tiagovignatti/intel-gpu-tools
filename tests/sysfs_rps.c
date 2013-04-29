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
 *
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "drmtest.h"

static bool verbose = false;
static int origmin, origmax;

#define restore_assert(COND) do { \
	if (!(COND)) { \
		writeval(stuff[MIN].filp, origmin); \
		writeval(stuff[MAX].filp, origmax); \
		assert(0); \
	} \
} while (0);

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

	fflush(filp);
	rewind(filp);
	scanned = fscanf(filp, "%d", &val);
	assert(scanned == 1);

	return val;
}

static int do_writeval(FILE *filp, int val, int lerrno)
{
	/* Must write twice to sysfs since the first one simply calculates the size and won't return the error */
	int ret;
	rewind(filp);
	ret = fprintf(filp, "%d", val);
	rewind(filp);
	ret = fprintf(filp, "%d", val);
	if (ret && lerrno)
		assert(errno = lerrno);
	fflush(filp);
	return ret;
}
#define writeval(filp, val) do_writeval(filp, val, 0)

#define fcur (readval(stuff[CUR].filp))
#define fmin (readval(stuff[MIN].filp))
#define fmax (readval(stuff[MAX].filp))
#define frp0 (readval(stuff[RP0].filp))
#define frp1 (readval(stuff[RP1].filp))
#define frpn (readval(stuff[RPn].filp))

static void setfreq(int val)
{
	writeval(stuff[MIN].filp, val);
	writeval(stuff[MAX].filp, val);
}

static void checkit(void)
{
	restore_assert(fmin <= fmax);
	restore_assert(fcur <= fmax);
	restore_assert(fmin <= fcur);
	restore_assert(frpn <= fmin);
	restore_assert(fmax <= frp0);
	restore_assert(frp1 <= frp0);
	restore_assert(frpn <= frp1);
	restore_assert(frp0 != 0);
	restore_assert(frp1 != 0);
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


int main(int argc, char *argv[])
{
	const int device = drm_get_card(0);
	struct junk *junk = stuff;
	int fd, ret;

	drmtest_skip_on_simulation();

	if (argc > 1)
		verbose++;

	/* Use drm_open_any to verify device existence */
	fd = drm_open_any();
	close(fd);

	do {
		int val = -1;
		char *path;
		ret = asprintf(&path, sysfs_base_path, device, junk->name);
		assert(ret != -1);
		junk->filp = fopen(path, junk->mode);
		if (junk->filp == NULL) {
			printf("Kernel is too old. GTFO\n");
			exit(77);
		}
		val = readval(junk->filp);
		assert(val >= 0);
		junk++;
	} while(junk->name != NULL);

	origmin = fmin;
	origmax = fmax;

	if (verbose)
		printf("Original min = %d\nOriginal max = %d\n", origmin, origmax);

	if (verbose)
		dumpit();

	checkit();
	setfreq(fmin);
	if (verbose)
		dumpit();
	restore_assert(fcur == fmin);
	setfreq(fmax);
	if (verbose)
		dumpit();
	restore_assert(fcur == fmax);
	checkit();

	/* And some errors */
	writeval(stuff[MIN].filp, frpn - 1);
	writeval(stuff[MAX].filp, frp0 + 1000);
	checkit();

	writeval(stuff[MIN].filp, fmax + 1000);
	writeval(stuff[MAX].filp, fmin - 1);
	checkit();

	do_writeval(stuff[MIN].filp, 0x11111110, EINVAL);
	do_writeval(stuff[MAX].filp, 0, EINVAL);

	writeval(stuff[MIN].filp, origmin);
	writeval(stuff[MAX].filp, origmax);

	exit(EXIT_SUCCESS);
}

/*
 * Copyright © 2015 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Example:
 * Get all frequencies:
 * intel_gpu_frequency --get
 *
 * Same as above:
 * intel_gpu_frequency -g
 *
 * Lock the GPU frequency to 300MHz:
 * intel_gpu_frequency --set 300
 *
 * Set the maximum frequency to 900MHz:
 * intel_gpu_frequency --custom max=900
 *
 * Lock the GPU frequency to its maximum frequency:
 * intel_gpu_frequency --max
 *
 * Lock the GPU frequency to its most efficient frequency:
 * intel_gpu_frequency -e
 *
 * Lock The GPU frequency to its minimum frequency:
 * intel_gpu_frequency --min
 *
 * Reset the GPU to hardware defaults
 * intel_gpu_frequency -d
 */

#define _GNU_SOURCE
#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "drmtest.h"
#include "intel_chipset.h"

#define VERSION "1.0"

static int device, devid;

enum {
	CUR=0,
	MIN,
	EFF,
	MAX,
	RP0,
	RPn
};

struct freq_info {
	const char *name;
	const char *mode;
	FILE *filp;
	char *path;
};

static struct freq_info info[] = {
	{ "cur", "r"  },
	{ "min", "rb+" },
	{ "RP1", "r" },
	{ "max", "rb+" },
	{ "RP0", "r" },
	{ "RPn", "r" }
};

static char *
get_sysfs_path(const char *which)
{
	static const char fmt[] = "/sys/class/drm/card%1d/gt_%3s_freq_mhz";
	char *path;
	int ret;

#define STATIC_STRLEN(string) (sizeof(string) / sizeof(string [0]))
	ret = asprintf(&path, fmt, device, which);
	assert(ret == (STATIC_STRLEN(fmt) - 3));
#undef STATIC_STRLEN

	return path;
}

static void
initialize_freq_info(struct freq_info *freq_info)
{
	if (freq_info->filp)
		return;

	freq_info->path = get_sysfs_path(freq_info->name);
	assert(freq_info->path);
	freq_info->filp = fopen(freq_info->path, freq_info->mode);
	assert(freq_info->filp);
}

static void wait_freq_settle(void)
{
	struct timespec ts;

	/* FIXME: Lazy sleep without check. */
	ts.tv_sec = 0;
	ts.tv_nsec = 20000;
	clock_nanosleep(CLOCK_MONOTONIC, 0, &ts, NULL);
}

static void set_frequency(struct freq_info *freq_info, int val)
{
	initialize_freq_info(freq_info);
	rewind(freq_info->filp);
	assert(fprintf(freq_info->filp, "%d", val) > 0);

	wait_freq_settle();
}

static int get_frequency(struct freq_info *freq_info)
{
	int val;

	initialize_freq_info(freq_info);
	rewind(freq_info->filp);
	assert(fscanf(freq_info->filp, "%d", &val)==1);

	return val;
}

static void __attribute__((noreturn))
usage(const char *prog)
{
	printf("%s A program to manipulate Intel GPU frequencies.\n\n", prog);
	printf("Usage: %s [-e] [--min | --max] [-g (min|max|efficient)] [-s frequency_mhz]\n\n", prog);
	printf("Options: \n");
	printf("  -e		Lock frequency to the most efficient frequency\n");
	printf("  -g, --get     Get all the frequency settings\n");
	printf("  -s, --set     Lock frequency to an absolute value (MHz)\n");
	printf("  -c, --custom  Set a min, or max frequency \"min=X | max=Y\"\n");
	printf("  -m  --max     Lock frequency to max frequency\n");
	printf("  -i  --min     Lock frequency to min (never a good idea, DEBUG ONLY)\n");
	printf("  -d  --defaults  Return the system to hardware defaults\n");
	printf("  -h  --help    Returns this\n");
	printf("  -v  --version Version\n");
	printf("\n");
	printf("Examples:\n");
	printf("   intel_gpu_frequency -gmin,cur\tGet the current and minimum frequency\n");
	printf("   intel_gpu_frequency -s 400\tLock frequency to 400Mhz\n");
	printf("   intel_gpu_frequency -c max=750\tSet the max frequency to 750MHz\n");
	printf("\n");
	printf("Report bugs to <bugs.freedesktop.org>\n");
	exit(EXIT_FAILURE);
}

static void
version(const char *prog)
{
	printf("%s: %s\n", prog, VERSION);
	printf("Copyright © 2015 Intel Corporation\n");
}

/* Returns read or write operation */
static bool
parse(int argc, char *argv[], bool *act_upon, size_t act_upon_n, int *new_freq)
{
	int c, tmp;
	bool write = false;

	/* No args means -g" */
	if (argc == 1) {
		for (c = 0; c < act_upon_n; c++)
			act_upon[c] = true;
		goto done;
	}
	while (1) {
		int option_index = 0;
		static struct option long_options[] = {
			{ "get", no_argument, NULL, 'g' },
			{ "set", required_argument, NULL, 's' },
			{ "custom", required_argument, NULL, 'c'},
			{ "min", no_argument, NULL, 'i' },
			{ "max", no_argument, NULL, 'm' },
			{ "defaults", no_argument, NULL, 'd' },
			{ "help", no_argument, NULL, 'h' },
			{ "version", no_argument, NULL, 'v' },
			{ NULL, 0, NULL, 0}
		};

		c = getopt_long(argc, argv, "egs:c:midh", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'g':
			if (write == true)
				fprintf(stderr, "Read and write operations not support simultaneously.\n");
			{
				int i;
				for (i = 0; i < act_upon_n; i++)
					act_upon[i] = true;
			}
			break;
		case 's':
			if (!optarg)
				usage(argv[0]);

			if (write == true) {
				fprintf(stderr, "Only one write may be specified at a time\n");
				exit(EXIT_FAILURE);
			}

			write = true;
			act_upon[MIN] = true;
			act_upon[MAX] = true;
			sscanf(optarg, "%d", &new_freq[MAX]);
			new_freq[MIN] = new_freq[MAX];
			break;
		case 'c':
			if (!optarg)
				usage(argv[0]);

			if (write == true) {
				fprintf(stderr, "Only one write may be specified at a time\n");
				exit(EXIT_FAILURE);
			}

			write = true;

			if (!strncmp("min=", optarg, 4)) {
				act_upon[MIN] = true;
				sscanf(optarg+4, "%d", &new_freq[MIN]);
			} else if (!strncmp("max=", optarg, 4)) {
				act_upon[MAX] = true;
				sscanf(optarg+4, "%d", &new_freq[MAX]);
			} else {
				fprintf(stderr, "Selected unmodifiable frequency\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'e': /* efficient */
			if (IS_VALLEYVIEW(devid) || IS_CHERRYVIEW(devid)) {
				/* the LP parts have special efficient frequencies */
				fprintf(stderr,
					"FIXME: Warning efficient frequency information is incorrect.\n");
				exit(EXIT_FAILURE);
			}
			tmp = get_frequency(&info[EFF]);
			new_freq[MIN] = tmp;
			new_freq[MAX] = tmp;
			act_upon[MIN] = true;
			act_upon[MAX] = true;
			write = true;
			break;
		case 'i': /* mIn */
			tmp = get_frequency(&info[RPn]);
			new_freq[MIN] = tmp;
			new_freq[MAX] = tmp;
			act_upon[MIN] = true;
			act_upon[MAX] = true;
			write = true;
			break;
		case 'm': /* max */
			tmp = get_frequency(&info[RP0]);
			new_freq[MIN] = tmp;
			new_freq[MAX] = tmp;
			act_upon[MIN] = true;
			act_upon[MAX] = true;
			write = true;
			break;
		case 'd': /* defaults */
			new_freq[MIN] = get_frequency(&info[RPn]);
			new_freq[MAX] = get_frequency(&info[RP0]);
			act_upon[MIN] = true;
			act_upon[MAX] = true;
			write = true;
			break;
		case 'v':
			version(argv[0]);
			exit(0);
		case 'h':
		default:
			usage(argv[0]);
		}
	}

done:
	return write;
}

int main(int argc, char *argv[])
{

	bool write, fail, targets[MAX+1] = {false};
	int i, try = 1, set_freq[MAX+1] = {0};

	devid = intel_get_drm_devid(drm_open_driver(DRIVER_INTEL));
	device = drm_get_card();

	write = parse(argc, argv, targets, ARRAY_SIZE(targets), set_freq);
	fail = write;

	/* If we've previously locked the frequency, we need to make sure to set things
	 * in the correct order, or else the operation will fail (ie. min = max = 200,
	 * and we set min to 300, we fail because it would try to set min >
	 * max). This can be accomplished be going either forward or reverse
	 * through the loop. MIN is always before MAX.
	 *
	 * XXX: Since only min and max are at play, the super lazy way is to do this
	 * 3 times and if we still fail after 3, it's for real.
	 */
again:
	if (try > 2) {
		fprintf(stderr, "Did not achieve desired freq.\n");
		exit(EXIT_FAILURE);
	}
	for (i = 0; i < ARRAY_SIZE(targets); i++) {
		if (targets[i] == false)
			continue;

		if (write) {
			set_frequency(&info[i], set_freq[i]);
			if (get_frequency(&info[i]) != set_freq[i])
				fail = true;
			else
				fail = false;
		} else {
			printf("%s: %d MHz\n", info[i].name, get_frequency(&info[i]));
		}
	}

	if (fail) {
		try++;
		goto again;
	}

	for (i = 0; i < ARRAY_SIZE(targets); i++) {
		if (info[i].filp) {
			fclose(info[i].filp);
			free(info[i].path);
		}
	}

	return EXIT_SUCCESS;
}

/*
 * Copyright © 2011 Intel Corporation
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
 * Authors:
 *    Ben Widawsky <ben@bwidawsk.net>
 *
 */

#include <assert.h>
#include <err.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>
#include "intel_io.h"
#include "intel_chipset.h"

bool daemonized;

#define INFO_PRINT(...) \
	do { \
		if (daemonized) \
			syslog(LOG_INFO, ##__VA_ARGS__); \
		else \
			fprintf(stdout, ##__VA_ARGS__); \
	} while(0)

static void
help(char *prog) {
	printf("%s Prevents the GT from sleeping.\n\n", prog);
	printf("usage: %s [options] \n\n", prog);
	printf("Options: \n");
	printf("    -b        Run in background/daemon mode\n");
}

static int
is_alive(void) {
	/* Read the timestamp, which should *almost* always be !0 */
	return (intel_register_read(0x2358) != 0);
}

int main(int argc, char *argv[])
{
	int ret;

	if (argc > 2 || (argc == 2 && !strncmp(argv[1], "-h", 2))) {
		help(argv[1]);
		exit(0);
	}

	if (argc == 2 && (!strncmp(argv[1], "-b", 2)))
		daemonized = true;

	if (daemonized) {
		assert(daemon(0, 0) == 0);
		openlog(argv[0], LOG_CONS | LOG_PID, LOG_USER);
		INFO_PRINT("started daemon");
	}

	ret = intel_register_access_init(intel_get_pci_device(), 1);
	if (ret) {
		INFO_PRINT("Couldn't init register access\n");
		exit(1);
	} else {
		INFO_PRINT("Forcewake locked\n");
	}
	while(1) {
		if (!is_alive()) {
			INFO_PRINT("gpu reset? restarting daemon\n");
			intel_register_access_fini();
			ret = intel_register_access_init(intel_get_pci_device(), 1);
			if (ret)
				INFO_PRINT("Reg access init fail\n");
		}
		sleep(1);
	}
	intel_register_access_fini();
	INFO_PRINT("Forcewake unlock\n");

	if (daemonized) {
		INFO_PRINT("finished\n");
		closelog();
	}

	return 0;
}

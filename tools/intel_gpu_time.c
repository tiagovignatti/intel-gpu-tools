/*
 * Copyright © 2007,2009 Intel Corporation
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
 *    Eric Anholt <eric@anholt.net>
 *    Chris Wilson <chris@chris-wilson.co.uk>
 *
 */

#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>

#include "intel_io.h"
#include "intel_chipset.h"
#include "intel_reg.h"

#define SAMPLES_PER_SEC             10000

static volatile int goddo;

static pid_t spawn(char **argv)
{
	pid_t pid;

	pid = fork();
	if (pid != 0)
		return pid;

	execvp(argv[0], argv);
	exit(1);
}

static void sighandler(int sig)
{
	goddo = sig;
}

int main(int argc, char **argv)
{
	pid_t child;
	uint64_t ring_idle = 0, ring_time = 0;
	struct timeval start, end;
	static struct rusage rusage;
	int status;

	intel_mmio_use_pci_bar(intel_get_pci_device());

	if (argc == 1) {
		fprintf(stderr, "usage: %s cmd [args...]\n", argv[0]);
		return 1;
	}

	signal(SIGCHLD, sighandler);
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);

	gettimeofday(&start, NULL);
	child = spawn(argv+1);
	if (child < 0)
		return 127;

	while (!goddo) {
		uint32_t ring_head, ring_tail;

		ring_head = INREG(LP_RING + RING_HEAD) & HEAD_ADDR;
		ring_tail = INREG(LP_RING + RING_TAIL) & TAIL_ADDR;

		if (ring_tail == ring_head)
			ring_idle++;
		ring_time++;

		usleep(1000000 / SAMPLES_PER_SEC);
	}
	gettimeofday(&end, NULL);
	timersub(&end, &start, &end);

	waitpid(child, &status, 0);

	getrusage(RUSAGE_CHILDREN, &rusage);
	printf("user: %ld.%06lds, sys: %ld.%06lds, elapsed: %ld.%06lds, CPU: %.1f%%, GPU: %.1f%%\n",
	       rusage.ru_utime.tv_sec, rusage.ru_utime.tv_usec,
	       rusage.ru_stime.tv_sec, rusage.ru_stime.tv_usec,
	       end.tv_sec, end.tv_usec,
	       100*(rusage.ru_utime.tv_sec + 1e-6*rusage.ru_utime.tv_usec + rusage.ru_stime.tv_sec + 1e-6*rusage.ru_stime.tv_usec) / (end.tv_sec + 1e-6*end.tv_usec),
	       100 - ring_idle * 100. / ring_time);

	return WEXITSTATUS(status);
}

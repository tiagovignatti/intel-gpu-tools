/*
 * Copyright © 2016 Intel Corporation
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

#define _GNU_SOURCE

#include "igt.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <limits.h>
#include "drm.h"

#include <linux/unistd.h>

#define gettid() syscall(__NR_gettid)
#define sigev_notify_thread_id _sigev_un._tid

static volatile int done;

struct gem_busyspin {
	pthread_t thread;
	int cpu;
	unsigned long count;
};

struct sys_wait {
	pthread_t thread;
	int cpu;
	struct igt_mean mean;
};

static void bind_cpu(pthread_t thread, int cpu)
{
	cpu_set_t mask;

	if (cpu == -1)
		return;

	CPU_ZERO(&mask);
	CPU_SET(cpu, &mask);

	pthread_setaffinity_np(thread, sizeof(mask), &mask);
}

static void force_low_latency(void)
{
	int32_t target = 0;
	int fd = open("/dev/cpu_dma_latency", O_RDWR);
	if (fd < 0 || write(fd, &target, sizeof(target)) < 0)
		fprintf(stderr,
			"Unable to prevent CPU sleeps and force low latency using /dev/cpu_dma_latency: %s\n",
			strerror(errno));
}

#define LOCAL_I915_EXEC_NO_RELOC (1<<11)
#define LOCAL_I915_EXEC_HANDLE_LUT (1<<12)

#define LOCAL_I915_EXEC_BSD_SHIFT      (13)
#define LOCAL_I915_EXEC_BSD_MASK       (3 << LOCAL_I915_EXEC_BSD_SHIFT)

#define ENGINE_FLAGS  (I915_EXEC_RING_MASK | LOCAL_I915_EXEC_BSD_MASK)

static bool ignore_engine(int fd, unsigned engine)
{
	if (engine == 0)
		return true;

	if (gem_has_bsd2(fd) && engine == I915_EXEC_BSD)
		return true;

	return false;
}

static void *gem_busyspin(void *arg)
{
	const uint32_t bbe = MI_BATCH_BUFFER_END;
	struct gem_busyspin *bs = arg;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj;
	unsigned engines[16];
	unsigned nengine;
	unsigned engine;
	int fd;

	bind_cpu(bs->thread, bs->cpu);

	fd = drm_open_driver(DRIVER_INTEL);

	nengine = 0;
	for_each_engine(fd, engine)
		if (!ignore_engine(fd, engine)) engines[nengine++] = engine;
	igt_require(nengine);

	memset(&obj, 0, sizeof(obj));
	obj.handle = gem_create(fd, 4096);
	gem_write(fd, obj.handle, 0, &bbe, sizeof(bbe));

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = (uintptr_t)&obj;
	execbuf.buffer_count = 1;
	execbuf.flags |= LOCAL_I915_EXEC_HANDLE_LUT;
	execbuf.flags |= LOCAL_I915_EXEC_NO_RELOC;
	if (__gem_execbuf(fd, &execbuf) == 0) {
		execbuf.flags = 0;
		gem_execbuf(fd, &execbuf);
	}

	while (!done) {
		for (int n = 0; n < nengine; n++) {
			execbuf.flags &= ~ENGINE_FLAGS;
			execbuf.flags |= engines[n];
			gem_execbuf(fd, &execbuf);
		}
		bs->count += nengine;
	}

	close(fd);
	return NULL;
}

#define MSEC_PER_SEC (1000)
#define USEC_PER_SEC (1000 * MSEC_PER_SEC)
#define NSEC_PER_SEC (1000 * USEC_PER_SEC)

static double elapsed(const struct timespec *a, const struct timespec *b)
{
	return 1e9*(b->tv_sec - a->tv_sec) + (b->tv_nsec - a ->tv_nsec);
}

static void *sys_wait(void *arg)
{
	struct sys_wait *w = arg;
	struct sigevent sev;
	timer_t timer;
	sigset_t mask;
	struct timespec now;
#define SIG SIGRTMIN

	bind_cpu(w->thread, w->cpu);

	sigemptyset(&mask);
	sigaddset(&mask, SIG);
	sigprocmask(SIG_SETMASK, &mask, NULL);

	sev.sigev_notify = SIGEV_SIGNAL | SIGEV_THREAD_ID;
	sev.sigev_notify_thread_id = gettid();
	sev.sigev_signo = SIG;
	timer_create(CLOCK_MONOTONIC, &sev, &timer);

	clock_gettime(CLOCK_MONOTONIC, &now);
	while (!done) {
		struct itimerspec its;
		int sigs;

		its.it_value = now;
		its.it_value.tv_nsec += 100 * 1000;
		its.it_value.tv_nsec += rand() % (NSEC_PER_SEC / 1000);
		if (its.it_value.tv_nsec >= NSEC_PER_SEC) {
			its.it_value.tv_nsec -= NSEC_PER_SEC;
			its.it_value.tv_sec += 1;
		}
		its.it_interval.tv_sec = its.it_interval.tv_nsec = 0;
		timer_settime(timer, TIMER_ABSTIME, &its, NULL);

		sigwait(&mask, &sigs);
		clock_gettime(CLOCK_MONOTONIC, &now);
		igt_mean_add(&w->mean, elapsed(&its.it_value, &now));
	}

	sigprocmask(SIG_UNBLOCK, &mask, NULL);
	timer_delete(timer);

	return NULL;
}

static void rtprio(pthread_attr_t *attr, int prio)
{
#ifdef PTHREAD_EXPLICIT_SCHED
	struct sched_param param = { .sched_priority = 99 };
	pthread_attr_setinheritsched(attr, PTHREAD_EXPLICIT_SCHED);
	pthread_attr_setschedpolicy(attr, SCHED_FIFO);
	pthread_attr_setschedparam(attr, &param);
#endif
}

static double l_estimate(igt_stats_t *stats)
{
	if (stats->n_values > 9)
		return igt_stats_get_trimean(stats);
	else if (stats->n_values > 5)
		return igt_stats_get_median(stats);
	else
		return igt_stats_get_mean(stats);
}

static double min_measurement_error(void)
{
	struct timespec start, end;
	int n;

	clock_gettime(CLOCK_MONOTONIC, &start);
	for (n = 0; n < 1024; n++)
		clock_gettime(CLOCK_MONOTONIC, &end);

	return elapsed(&start, &end) / n;
}

int main(int argc, char **argv)
{
	struct gem_busyspin *busy;
	struct sys_wait *wait;
	pthread_attr_t attr;
	int ncpus = sysconf(_SC_NPROCESSORS_ONLN);
	igt_stats_t cycles, mean, max;
	double min;
	int time = 10;
	int field = -1;
	int enable_gem_sysbusy = 1;
	int n, c;

	while ((c = getopt(argc, argv, "t:f:n")) != -1) {
		switch (c) {
		case 'n': /* dry run, measure baseline system latency */
			enable_gem_sysbusy = 0;
			break;
		case 't':
			/* How long to run the benchmark for (seconds) */
			time = atoi(optarg);
			if (time < 0)
				time = INT_MAX;
			break;
		case 'f':
			/* Select an output field */
			field = atoi(optarg);
			break;
		default:
			break;
		}
	}

	/* Prevent CPU sleeps so that busy and idle loads are consistent. */
	force_low_latency();
	min = min_measurement_error();

	busy = calloc(ncpus, sizeof(*busy));
	if (enable_gem_sysbusy) {
		for (n = 0; n < ncpus; n++) {
			busy[n].cpu = n;
			pthread_create(&busy[n].thread, NULL,
				       gem_busyspin, &busy[n]);
		}
	}

	wait = calloc(ncpus, sizeof(*wait));
	pthread_attr_init(&attr);
	rtprio(&attr, 99);
	for (n = 0; n < ncpus; n++) {
		wait[n].cpu = n;
		igt_mean_init(&wait[n].mean);
		pthread_create(&wait[n].thread, &attr, sys_wait, &wait[n]);
	}

	sleep(time);
	done = 1;

	igt_stats_init_with_size(&cycles, ncpus);
	if (enable_gem_sysbusy) {
		for (n = 0; n < ncpus; n++) {
			pthread_join(busy[n].thread, NULL);
			igt_stats_push(&cycles, busy[n].count);
		}
	}

	igt_stats_init_with_size(&mean, ncpus);
	igt_stats_init_with_size(&max, ncpus);
	for (n = 0; n < ncpus; n++) {
		pthread_join(wait[n].thread, NULL);
		igt_stats_push_float(&mean, wait[n].mean.mean);
		igt_stats_push_float(&max, wait[n].mean.max);
	}

	switch (field) {
	default:
		printf("gem_syslatency: cycles=%.0f, latency mean=%.3fus max=%.0fus\n",
		       igt_stats_get_mean(&cycles),
		       (igt_stats_get_mean(&mean) - min)/ 1000,
		       (l_estimate(&max) - min) / 1000);
		break;
	case 0:
		printf("%.0f\n", igt_stats_get_mean(&cycles));
		break;
	case 1:
		printf("%.3f\n", (igt_stats_get_mean(&mean) - min) / 1000);
		break;
	case 2:
		printf("%.0f\n", (l_estimate(&max) - min) / 1000);
		break;
	}

	return 0;

}

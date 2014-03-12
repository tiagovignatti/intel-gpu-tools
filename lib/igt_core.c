/*
 * Copyright © 2007, 2011, 2013, 2014 Intel Corporation
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
 *    Eric Anholt <eric@anholt.net>
 *    Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 */

#ifndef ANDROID
#define _GNU_SOURCE
#else
#include <libgen.h>
#endif
#include <stdio.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <string.h>
#include <sys/mman.h>
#include <signal.h>
#include <pciaccess.h>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <termios.h>

#include "drmtest.h"
#include "i915_drm.h"
#include "intel_chipset.h"
#include "intel_gpu_tools.h"
#include "igt_debugfs.h"
#include "../version.h"
#include "config.h"

#include "igt_core.h"

/**
 * SECTION:igt_core
 * @short_description: Core i-g-t testing support
 * @title: i-g-t core
 *
 * This libary implements the core of the i-g-t test support infrastructure.
 * Main features are the subtest enumeration, cmdline option parsing helpers for
 * subtest handling and various helpers to structure testcases with subtests and
 * handle subtest test results.
 *
 * Auxiliary code provides exit handlers, support for forked processes with test
 * result propagation. Other generally useful functionality includes optional
 * structure logging infrastructure and some support code for running reduced
 * test set on in simulated hardware environments.
 *
 * When writing tests with subtests it is extremely important that nothing
 * interferes with the subtest enumeration. In i-g-t subtests are enumerated at
 * runtime, which allows powerful testcase enumeration. But it makes subtest
 * enumeration a bit more tricky since the test code needs to be careful to
 * never run any code which might fail (like trying to do privileged operations
 * or opening device driver nodes).
 *
 * To allow this i-g-t provides #igt_fixture code blocks for setup code outside
 * of subtests and automatically skips the subtest code blocks themselves. For
 * special cases igt_only_list_subtests() is also provided.
 */

static unsigned int exit_handler_count;

/* subtests helpers */
static bool list_subtests = false;
static char *run_single_subtest = NULL;
static const char *in_subtest = NULL;
static bool in_fixture = false;
static bool test_with_subtests = false;
static enum {
	CONT = 0, SKIP, FAIL
} skip_subtests_henceforth = CONT;

/* fork support state */
pid_t *test_children;
int num_test_children;
int test_children_sz;
bool test_child;

bool __igt_fixture(void)
{
	assert(!in_fixture);

	if (igt_only_list_subtests())
		return false;

	if (skip_subtests_henceforth)
		return false;

	in_fixture = true;
	return true;
}

void __igt_fixture_complete(void)
{
	assert(in_fixture);

	in_fixture = false;
}

void __igt_fixture_end(void)
{
	assert(in_fixture);

	in_fixture = false;
	longjmp(igt_subtest_jmpbuf, 1);
}

bool igt_exit_called;
static void check_igt_exit(int sig)
{
	/* When not killed by a signal check that igt_exit() has been properly
	 * called. */
	assert(sig != 0 || igt_exit_called);
}

static void print_version(void)
{
	struct utsname uts;

	if (list_subtests)
		return;

	uname(&uts);

	fprintf(stdout, "IGT-Version: %s-%s (%s) (%s: %s %s)\n", PACKAGE_VERSION,
		IGT_GIT_SHA1, TARGET_CPU_PLATFORM,
		uts.sysname, uts.release, uts.machine);
}

static void print_usage(const char *command_str, const char *help_str,
			bool output_on_stderr)
{
	FILE *f = output_on_stderr ? stderr : stdout;

	fprintf(f, "Usage: %s [OPTIONS]\n"
		   "  --list-subtests\n"
		   "  --run-subtest <pattern>\n", command_str);
	if (help_str)
		fprintf(f, "%s\n", help_str);
}

static void oom_adjust_for_doom(void)
{
	int fd;
	const char always_kill[] = "1000";

	fd = open("/proc/self/oom_score_adj", O_WRONLY);
	igt_assert(fd != -1);
	igt_assert(write(fd, always_kill, sizeof(always_kill)) == sizeof(always_kill));
}

/**
 * igt_subtest_init_parse_opts:
 * @argc: argc from the test's main()
 * @argv: argv from the test's main()
 * @extra_short_opts: getopt_long() compliant list with additional short options
 * @extra_long_opts: getopt_long() compliant list with additional long options
 * @help_str: help string for the additional options
 * @extra_opt_handler: handler for the additional options
 *
 * This function handles the subtest related cmdline options and allows an
 * arbitrary set of additional options. This is useful for tests which have
 * additional knobs to tune when run manually like the number of rounds execute
 * or the size of the allocated buffer objects.
 *
 * Tests without special needs should just use igt_subtest_init() or use
 * #igt_main directly instead of their own main() function.
 *
 * Returns: Forwards any option parsing errors from getopt_long.
 */
int igt_subtest_init_parse_opts(int argc, char **argv,
				const char *extra_short_opts,
				struct option *extra_long_opts,
				const char *help_str,
				igt_opt_handler_t extra_opt_handler)
{
	int c, option_index = 0;
	static struct option long_options[] = {
		{"list-subtests", 0, 0, 'l'},
		{"run-subtest", 1, 0, 'r'},
		{"help", 0, 0, 'h'},
	};
	const char *command_str;
	char *short_opts;
	struct option *combined_opts;
	int extra_opt_count;
	int all_opt_count;
	int ret = 0;

	test_with_subtests = true;

	command_str = argv[0];
	if (strrchr(command_str, '/'))
		command_str = strrchr(command_str, '/') + 1;

	/* First calculate space for all passed-in extra long options */
	all_opt_count = 0;
	while (extra_long_opts && extra_long_opts[all_opt_count].name)
		all_opt_count++;
	extra_opt_count = all_opt_count;

	all_opt_count += ARRAY_SIZE(long_options);

	combined_opts = malloc(all_opt_count * sizeof(*combined_opts));
	memcpy(combined_opts, extra_long_opts,
	       extra_opt_count * sizeof(*combined_opts));

	/* Copy the subtest long options (and the final NULL entry) */
	memcpy(&combined_opts[extra_opt_count], long_options,
		ARRAY_SIZE(long_options) * sizeof(*combined_opts));

	ret = asprintf(&short_opts, "%sh",
		       extra_short_opts ? extra_short_opts : "");
	assert(ret >= 0);

	while ((c = getopt_long(argc, argv, short_opts, combined_opts,
			       &option_index)) != -1) {
		switch(c) {
		case 'l':
			if (!run_single_subtest)
				list_subtests = true;
			break;
		case 'r':
			if (!list_subtests)
				run_single_subtest = strdup(optarg);
			break;
		case 'h':
			print_usage(command_str, help_str, false);
			ret = -1;
			goto out;
		case '?':
			if (opterr) {
				print_usage(command_str, help_str, true);
				ret = -2;
				goto out;
			}
			/*
			 * Just ignore the error, since the unknown argument
			 * can be something the caller understands and will
			 * parse by doing a second getopt scanning.
			 */
			break;
		default:
			ret = extra_opt_handler(c, option_index);
			if (ret)
				goto out;
		}
	}

	igt_install_exit_handler(check_igt_exit);
	oom_adjust_for_doom();

out:
	free(short_opts);
	free(combined_opts);
	print_version();

	return ret;
}

enum igt_log_level igt_log_level = IGT_LOG_INFO;

static void common_init(void)
{
	char *env = getenv("IGT_LOG_LEVEL");

	if (!env)
		return;

	if (strcmp(env, "debug") == 0)
		igt_log_level = IGT_LOG_DEBUG;
	else if (strcmp(env, "info") == 0)
		igt_log_level = IGT_LOG_INFO;
	else if (strcmp(env, "warn") == 0)
		igt_log_level = IGT_LOG_WARN;
	else if (strcmp(env, "none") == 0)
		igt_log_level = IGT_LOG_NONE;
}

/**
 * igt_subtest_init:
 * @argc: argc from the test's main()
 * @argv: argv from the test's main()
 *
 * This initializes the for tests with subtests without the need for additional
 * cmdline options. It is just a simplified version of
 * igt_subtest_init_parse_opts().
 *
 * If there's not a reason to the contrary it's less error prone to just use an
 * #igt_main block instead of stitching the tests's main() function together
 * manually.
 */
void igt_subtest_init(int argc, char **argv)
{
	int ret;

	/* supress getopt errors about unknown options */
	opterr = 0;

	ret = igt_subtest_init_parse_opts(argc, argv, NULL, NULL, NULL, NULL);
	if (ret < 0)
		/* exit with no error for -h/--help */
		exit(ret == -1 ? 0 : ret);

	/* reset opt parsing */
	optind = 1;

	common_init();
}

/**
 * igt_simple_init:
 *
 * This initializes a simple test without any support for subtests.
 *
 * If there's not a reason to the contrary it's less error prone to just use an
 * #igt_simple_main block instead of stitching the tests's main() function together
 * manually.
 */
void igt_simple_init(void)
{
	print_version();

	oom_adjust_for_doom();

	common_init();
}

/*
 * Note: Testcases which use these helpers MUST NOT output anything to stdout
 * outside of places protected by igt_run_subtest checks - the piglit
 * runner adds every line to the subtest list.
 */
bool __igt_run_subtest(const char *subtest_name)
{
	assert(!in_subtest);
	assert(!in_fixture);

	if (list_subtests) {
		printf("%s\n", subtest_name);
		return false;
	}

	if (run_single_subtest &&
	    strcmp(subtest_name, run_single_subtest) != 0)
		return false;

	if (skip_subtests_henceforth) {
		printf("Subtest %s: %s\n", subtest_name,
		       skip_subtests_henceforth == SKIP ?
		       "SKIP" : "FAIL");
		return false;
	}

	return (in_subtest = subtest_name);
}

/**
 * igt_subtest_name:
 *
 * Returns: The name of the currently executed subtest or NULL if called from
 * outside a subtest block.
 */
const char *igt_subtest_name(void)
{
	return in_subtest;
}

/**
 * igt_only_list_subtests:
 *
 * Returns: Returns true if only subtest should be listed and any setup code
 * must be skipped, false otherwise.
 */
bool igt_only_list_subtests(void)
{
	return list_subtests;
}

static bool skipped_one = false;
static bool succeeded_one = false;
static bool failed_one = false;
static int igt_exitcode;

static void exit_subtest(const char *) __attribute__((noreturn));
static void exit_subtest(const char *result)
{
	printf("Subtest %s: %s\n", in_subtest, result);
	in_subtest = NULL;
	longjmp(igt_subtest_jmpbuf, 1);
}

/**
 * igt_skip:
 * @f: format string
 * @...: optional arguments used in the format string
 *
 * Subtest aware test skipping. The format string is printed to stderr as the
 * reason why the test skipped.
 *
 * For tests with subtests this will either bail out of the current subtest or
 * mark all subsequent subtests as SKIP (presuming some global setup code
 * failed).
 *
 * For normal tests without subtest it will directly exit.
 */
void igt_skip(const char *f, ...)
{
	va_list args;
	skipped_one = true;

	assert(!test_child);

	if (!igt_only_list_subtests()) {
		va_start(args, f);
		vprintf(f, args);
		va_end(args);
	}

	if (in_subtest) {
		exit_subtest("SKIP");
	} else if (test_with_subtests) {
		skip_subtests_henceforth = SKIP;
		assert(in_fixture);
		__igt_fixture_end();
	} else {
		exit(77);
	}
}

void __igt_skip_check(const char *file, const int line,
		      const char *func, const char *check,
		      const char *f, ...)
{
	va_list args;
	int err = errno;

	if (f) {
		static char *buf;

		/* igt_skip never returns, so try to not leak too badly. */
		if (buf)
			free(buf);

		va_start(args, f);
		vasprintf(&buf, f, args);
		va_end(args);

		igt_skip("Test requirement not met in function %s, file %s:%i:\n"
			 "Last errno: %i, %s\n"
			 "Test requirement: (%s)\n%s",
			 func, file, line, err, strerror(err), check, buf);
	} else {
		igt_skip("Test requirement not met in function %s, file %s:%i:\n"
			 "Last errno: %i, %s\n"
			 "Test requirement: (%s)\n",
			 func, file, line, err, strerror(err), check);
	}
}

/**
 * igt_success:
 *
 * Complete a (subtest) as successfull
 *
 * This bails out of a subtests and marks it as successful. For global tests it
 * it won't bail out of anything.
 */
void igt_success(void)
{
	succeeded_one = true;
	if (in_subtest)
		exit_subtest("SUCCESS");
}

/**
 * igt_fail:
 * @exitcode: exitcode
 *
 * Fail a testcase. The exitcode is used as the exit code of the test process.
 * It may not be 0 (which indicates success) or 77 (which indicates a skipped
 * test).
 *
 * For tests with subtests this will either bail out of the current subtest or
 * mark all subsequent subtests as FAIL (presuming some global setup code
 * failed).
 *
 * For normal tests without subtest it will directly exit with the given
 * exitcode.
 */
void igt_fail(int exitcode)
{
	assert(exitcode != 0 && exitcode != 77);

	if (!failed_one)
		igt_exitcode = exitcode;

	failed_one = true;

	/* Silent exit, parent will do the yelling. */
	if (test_child)
		exit(exitcode);

	if (in_subtest)
		exit_subtest("FAIL");
	else {
		assert(!test_with_subtests || in_fixture);

		if (in_fixture) {
			skip_subtests_henceforth = FAIL;
			__igt_fixture_end();
		}

		exit(exitcode);
	}
}

static bool run_under_gdb(void)
{
	char buf[1024];

	sprintf(buf, "/proc/%d/exe", getppid());
	return (readlink (buf, buf, sizeof (buf)) != -1 &&
		strncmp(basename(buf), "gdb", 3) == 0);
}

void __igt_fail_assert(int exitcode, const char *file,
		       const int line, const char *func, const char *assertion,
		       const char *f, ...)
{
	va_list args;
	int err = errno;

	printf("Test assertion failure function %s, file %s:%i:\n"
	       "Last errno: %i, %s\n"
	       "Failed assertion: %s\n",
	       func, file, line, err, strerror(err), assertion);

	if (f) {
		va_start(args, f);
		vprintf(f, args);
		va_end(args);
	}

	if (run_under_gdb())
		abort();
	igt_fail(exitcode);
}

/**
 * igt_exit:
 *
 * exit() for both types (simple and with subtests) of i-g-t tests.
 *
 * This will exit the test with the right exit code when subtests have been
 * skipped. For normal tests it exits with a successful exit code, presuming
 * everything has worked out. For subtests it also checks that at least one
 * subtest has been run (save when only listing subtests.
 *
 * It is an error to normally exit a test with subtests without calling
 * igt_exit() - without it the result reporting will be wrong. To avoid such
 * issues it is highly recommended to use #igt_main instead of a hand-rolled
 * main() function.
 */
void igt_exit(void)
{
	igt_exit_called = true;

	if (igt_only_list_subtests())
		exit(0);

	if (!test_with_subtests)
		exit(0);

	/* Calling this without calling one of the above is a failure */
	assert(skipped_one || succeeded_one || failed_one);

	if (failed_one)
		exit(igt_exitcode);
	else if (succeeded_one)
		exit(0);
	else
		exit(77);
}

/* fork support code */
static int helper_process_count;
static pid_t helper_process_pids[] =
{ -1, -1, -1, -1};

static void reset_helper_process_list(void)
{
	for (int i = 0; i < ARRAY_SIZE(helper_process_pids); i++)
		helper_process_pids[i] = -1;
	helper_process_count = 0;
}

static void fork_helper_exit_handler(int sig)
{
	for (int i = 0; i < ARRAY_SIZE(helper_process_pids); i++) {
		pid_t pid = helper_process_pids[i];
		int status, ret;

		if (pid != -1) {
			/* Someone forgot to fill up the array? */
			assert(pid != 0);

			ret = kill(pid, SIGQUIT);
			assert(ret == 0);
			while (waitpid(pid, &status, 0) == -1 &&
			       errno == EINTR)
				;
			helper_process_count--;
		}
	}

	assert(helper_process_count == 0);
}

bool __igt_fork_helper(struct igt_helper_process *proc)
{
	pid_t pid;
	int id;

	assert(!proc->running);
	assert(helper_process_count < ARRAY_SIZE(helper_process_pids));

	for (id = 0; helper_process_pids[id] != -1; id++)
		;

	igt_install_exit_handler(fork_helper_exit_handler);

	switch (pid = fork()) {
	case -1:
		igt_assert(0);
	case 0:
		exit_handler_count = 0;
		reset_helper_process_list();
		oom_adjust_for_doom();

		return true;
	default:
		proc->running = true;
		proc->pid = pid;
		proc->id = id;
		helper_process_pids[id] = pid;
		helper_process_count++;

		return false;
	}

}

/**
 * igt_stop_helper:
 * @proc: #igt_helper_process structure
 *
 * Terminates a helper process. It is an error to call this on a helper process
 * which hasn't been spawned yet.
 */
void igt_stop_helper(struct igt_helper_process *proc)
{
	int status, ret;

	assert(proc->running);

	ret = kill(proc->pid,
		   proc->use_SIGKILL ? SIGKILL : SIGQUIT);
	assert(ret == 0);
	while (waitpid(proc->pid, &status, 0) == -1 &&
	       errno == EINTR)
		;
	igt_assert(WIFSIGNALED(status) &&
		   WTERMSIG(status) == (proc->use_SIGKILL ? SIGKILL : SIGQUIT));

	proc->running = false;

	helper_process_pids[proc->id] = -1;
	helper_process_count--;
}

/**
 * igt_wait_helper:
 * @proc: #igt_helper_process structure
 *
 * Joins a helper process. It is an error to call this on a helper process which
 * hasn't been spawned yet.
 */
void igt_wait_helper(struct igt_helper_process *proc)
{
	int status;

	assert(proc->running);

	while (waitpid(proc->pid, &status, 0) == -1 &&
	       errno == EINTR)
		;
	igt_assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);

	proc->running = false;

	helper_process_pids[proc->id] = -1;
	helper_process_count--;
}

static void children_exit_handler(int sig)
{
	int ret;

	assert(!test_child);

	for (int nc = 0; nc < num_test_children; nc++) {
		int status = -1;
		ret = kill(test_children[nc], SIGQUIT);
		assert(ret == 0);

		while (waitpid(test_children[nc], &status, 0) == -1 &&
		       errno == EINTR)
			;
	}

	num_test_children = 0;
}

bool __igt_fork(void)
{
	assert(!test_with_subtests || in_subtest);
	assert(!test_child);

	igt_install_exit_handler(children_exit_handler);

	if (num_test_children >= test_children_sz) {
		if (!test_children_sz)
			test_children_sz = 4;
		else
			test_children_sz *= 2;

		test_children = realloc(test_children,
					sizeof(pid_t)*test_children_sz);
		igt_assert(test_children);
	}

	switch (test_children[num_test_children++] = fork()) {
	case -1:
		igt_assert(0);
	case 0:
		test_child = true;
		exit_handler_count = 0;
		reset_helper_process_list();
		oom_adjust_for_doom();

		return true;
	default:
		return false;
	}

}

/**
 * igt_waitchildren:
 *
 * Wait for all children forked with igt_fork.
 *
 * The magic here is that exit codes from children will be correctly propagated
 * to the main thread, including the relevant exitcode if a child thread failed.
 * Of course if multiple children failed with differen exitcodes the resulting
 * exitcode will be non-deterministic.
 *
 * Note that igt_skip() will not be forwarded, feature tests need to be done
 * before spawning threads with igt_fork().
 */
void igt_waitchildren(void)
{
	assert(!test_child);

	for (int nc = 0; nc < num_test_children; nc++) {
		int status = -1;
		while (waitpid(test_children[nc], &status, 0) == -1 &&
		       errno == EINTR)
			;

		if (status != 0) {
			if (WIFEXITED(status)) {
				printf("child %i failed with exit status %i\n",
				       nc, WEXITSTATUS(status));
				igt_fail(WEXITSTATUS(status));
			} else if (WIFSIGNALED(status)) {
				printf("child %i died with signal %i, %s\n",
				       nc, WTERMSIG(status),
				       strsignal(WTERMSIG(status)));
				igt_fail(99);
			} else {
				printf("Unhandled failure in child %i\n", nc);
				abort();
			}
		}
	}

	num_test_children = 0;
}

/* exit handler code */
#define MAX_SIGNALS		32
#define MAX_EXIT_HANDLERS	5

static struct {
	sighandler_t handler;
	bool installed;
} orig_sig[MAX_SIGNALS];

static igt_exit_handler_t exit_handler_fn[MAX_EXIT_HANDLERS];
static bool exit_handler_disabled;
static sigset_t saved_sig_mask;
static const int handled_signals[] =
	{ SIGINT, SIGHUP, SIGTERM, SIGQUIT, SIGPIPE, SIGABRT, SIGSEGV, SIGBUS };

static int install_sig_handler(int sig_num, sighandler_t handler)
{
	orig_sig[sig_num].handler = signal(sig_num, handler);

	if (orig_sig[sig_num].handler == SIG_ERR)
		return -1;

	orig_sig[sig_num].installed = true;

	return 0;
}

static void restore_sig_handler(int sig_num)
{
	/* Just restore the default so that we properly fall over. */
	signal(sig_num, SIG_DFL);
}

static void restore_all_sig_handler(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(orig_sig); i++)
		restore_sig_handler(i);
}

static void call_exit_handlers(int sig)
{
	int i;

	if (!exit_handler_count) {
		return;
	}

	for (i = exit_handler_count - 1; i >= 0; i--)
		exit_handler_fn[i](sig);

	/* ensure we don't get called twice */
	exit_handler_count = 0;
}

static void igt_atexit_handler(void)
{
	restore_all_sig_handler();

	if (!exit_handler_disabled)
		call_exit_handlers(0);
}

static void fatal_sig_handler(int sig)
{
	pid_t pid, tid;

	restore_all_sig_handler();

	/*
	 * exit_handler_disabled is always false here, since when we set it
	 * we also block signals.
	 */
	call_exit_handlers(sig);

	/* Workaround cached PID and TID races on glibc and Bionic libc. */
	pid = syscall(SYS_getpid);
	tid = syscall(SYS_gettid);

	syscall(SYS_tgkill, pid, tid, sig);
}

/**
 * igt_install_exit_handler:
 * @fn: exit handler function
 *
 * Set a handler that will be called either when the process calls exit() or
 * returns from the main function, or one of the signals in 'handled_signals'
 * is raised. MAX_EXIT_HANDLERS handlers can be installed, each of which will
 * be called only once, even if a subsequent signal is raised. If the exit
 * handlers are called due to a signal, the signal will be re-raised with the
 * original signal disposition after all handlers returned.
 *
 * The handler will be passed the signal number if called due to a signal, or
 * 0 otherwise. Exit handlers can also be used from test children spawned with
 * igt_fork(), but not from within helper processes spawned with
 * igt_helper_process(). The list of exit handlers is reset when forking to
 * avoid issues with children cleanup up the parent's state too early.
 */
void igt_install_exit_handler(igt_exit_handler_t fn)
{
	int i;

	for (i = 0; i < exit_handler_count; i++)
		if (exit_handler_fn[i] == fn)
			return;

	igt_assert(exit_handler_count < MAX_EXIT_HANDLERS);

	exit_handler_fn[exit_handler_count] = fn;
	exit_handler_count++;

	if (exit_handler_count > 1)
		return;

	for (i = 0; i < ARRAY_SIZE(handled_signals); i++) {
		if (install_sig_handler(handled_signals[i],
					fatal_sig_handler))
			goto err;
	}

	if (atexit(igt_atexit_handler))
		goto err;

	return;
err:
	restore_all_sig_handler();
	exit_handler_count--;

	igt_assert_f(0, "failed to install the signal handler\n");
}

/**
 * igt_disable_exit_handler:
 *
 * Temporarily disable all exit handlers. Useful for library code doing tricky
 * things.
 */
void igt_disable_exit_handler(void)
{
	sigset_t set;
	int i;

	if (exit_handler_disabled)
		return;

	sigemptyset(&set);
	for (i = 0; i < ARRAY_SIZE(handled_signals); i++)
		sigaddset(&set, handled_signals[i]);

	if (sigprocmask(SIG_BLOCK, &set, &saved_sig_mask)) {
		perror("sigprocmask");
		return;
	}

	exit_handler_disabled = true;
}

/**
 * igt_enable_exit_handler:
 *
 * Re-enable all exit handlers temporarily disabled with
 * igt_disable_exit_handler().
 */
void igt_enable_exit_handler(void)
{
	if (!exit_handler_disabled)
		return;

	if (sigprocmask(SIG_SETMASK, &saved_sig_mask, NULL)) {
		perror("sigprocmask");
		return;
	}

	exit_handler_disabled = false;
}

/* simulation enviroment support */

/**
 * igt_run_in_simulation:
 *
 * This function can be used to select a reduced test set when running in
 * simulation enviroments. This i-g-t mode is selected by setting the
 * INTEL_SIMULATION enviroment variable to 1.
 *
 * Returns: True when run in simulation mode, false otherwise.
 */
bool igt_run_in_simulation(void)
{
	static int simulation = -1;

	if (simulation == -1)
		simulation = igt_env_set("INTEL_SIMULATION", false);

	return simulation;
}

/**
 * igt_skip_on_simulation:
 *
 * Skip tests when INTEL_SIMULATION environment variable is set. It uses
 * igt_skip() internally and hence is fully subtest aware.
 */
void igt_skip_on_simulation(void)
{
	if (igt_only_list_subtests())
		return;

	igt_require(!igt_run_in_simulation());
}

/* structured logging */

/**
 * igt_log:
 * @level: #igt_log_level
 * @format: format string
 * @...: optional arguments used in the format string
 *
 * This is the generic structure logging helper function. i-g-t testcase should
 * output all normal message to stdout. Warning level message should be printed
 * to stderr and the test runner should treat this as an intermediate result
 * between SUCESS and FAILURE.
 *
 * The log level can be set through the IGT_LOG_LEVEL enviroment variable with
 * values "debug", "info", "warn" and "none". By default verbose debug message
 * are disabled. "none" completely disables all output and is not recommended
 * since crucial issues only reported at the IGT_LOG_WARN level are ignored.
 */
void igt_log(enum igt_log_level level, const char *format, ...)
{
	va_list args;

	assert(format);

	if (igt_log_level > level)
		return;

	va_start(args, format);
	if (level == IGT_LOG_WARN) {
		fflush(stdout);
		vfprintf(stderr, format, args);
	} else
		vprintf(format, args);
	va_end(args);
}

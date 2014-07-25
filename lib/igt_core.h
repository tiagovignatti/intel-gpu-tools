/*
 * Copyright © 2007,2014 Intel Corporation
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


#ifndef IGT_CORE_H
#define IGT_CORE_H

#include <setjmp.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdarg.h>
#include <getopt.h>

/**
 * IGT_EXIT_TIMEOUT:
 *
 * Exit status indicating a timeout occurred.
 */
#define IGT_EXIT_TIMEOUT 78

/**
 * IGT_EXIT_SKIP:
 *
 * Exit status indicating the test was skipped.
 */
#define IGT_EXIT_SKIP    77

/**
 * IGT_EXIT_SUCCESS
 *
 * Exit status indicating the test executed successfully.
 */
#define IGT_EXIT_SUCCESS 0

/**
 * IGT_EXIT_INVALID
 *
 * Exit status indicating an invalid option or subtest was specified
 */
#define IGT_EXIT_INVALID 79


bool __igt_fixture(void);
void __igt_fixture_complete(void);
void __igt_fixture_end(void) __attribute__((noreturn));
/**
 * igt_fixture:
 *
 * Annotate global test fixture code
 *
 * Testcase with subtests often need to set up a bunch of global state as the
 * common test fixture. To avoid such code interferring with the subtest
 * enumeration (e.g. when enumerating on systemes without an intel gpu) such
 * blocks should be annotated with igt_fixture.
 */
#define igt_fixture for (int igt_tokencat(__tmpint,__LINE__) = 0; \
			 igt_tokencat(__tmpint,__LINE__) < 1 && \
			 __igt_fixture() && \
			 (setjmp(igt_subtest_jmpbuf) == 0); \
			 igt_tokencat(__tmpint,__LINE__) ++, \
			 __igt_fixture_complete())

/* subtest infrastructure */
jmp_buf igt_subtest_jmpbuf;
void igt_subtest_init(int argc, char **argv);
typedef int (*igt_opt_handler_t)(int opt, int opt_index);
#ifndef __GTK_DOC_IGNORE__ /* gtkdoc wants to document this forward decl */
struct option;
#endif
int igt_subtest_init_parse_opts(int argc, char **argv,
				const char *extra_short_opts,
				struct option *extra_long_opts,
				const char *help_str,
				igt_opt_handler_t extra_opt_handler);

bool __igt_run_subtest(const char *subtest_name);
#define __igt_tokencat2(x, y) x ## y

/**
 * igt_tokencat:
 * @x: first variable
 * @y: second variable
 *
 * C preprocessor helper to concatenate two variables while properly expanding
 * them.
 */
#define igt_tokencat(x, y) __igt_tokencat2(x, y)

/**
 * igt_subtest:
 * @name: name of the subtest
 *
 * This is a magic control flow block which denotes a subtest code block. Within
 * that codeblock igt_skip|success will only bail out of the subtest. The _f
 * variant accepts a printf format string, which is useful for constructing
 * combinatorial tests.
 *
 * This is a simpler version of igt_subtest_f()
 */
#define igt_subtest(name) for (; __igt_run_subtest((name)) && \
				   (setjmp(igt_subtest_jmpbuf) == 0); \
				   igt_success())
#define __igt_subtest_f(tmp, format...) \
	for (char tmp [256]; \
	     snprintf( tmp , sizeof( tmp ), \
		      format), \
	     __igt_run_subtest( tmp ) && \
	     (setjmp(igt_subtest_jmpbuf) == 0); \
	     igt_success())

/**
 * igt_subtest_f:
 * @...: format string and optional arguments
 *
 * This is a magic control flow block which denotes a subtest code block. Within
 * that codeblock igt_skip|success will only bail out of the subtest. The _f
 * variant accepts a printf format string, which is useful for constructing
 * combinatorial tests.
 *
 * Like igt_subtest(), but also accepts a printf format string instead of a
 * static string.
 */
#define igt_subtest_f(f...) \
	__igt_subtest_f(igt_tokencat(__tmpchar, __LINE__), f)

const char *igt_subtest_name(void);
bool igt_only_list_subtests(void);

/**
 * igt_main:
 *
 * This is a magic control flow block used instead of a main() function for
 * tests with subtests. Open-coding the main() function is only recommended if
 * the test needs to parse additional cmdline arguments of its own.
 */
#define igt_main \
	static void igt_tokencat(__real_main, __LINE__)(void); \
	int main(int argc, char **argv) { \
		igt_subtest_init(argc, argv); \
		igt_tokencat(__real_main, __LINE__)(); \
		igt_exit(); \
	} \
	static void igt_tokencat(__real_main, __LINE__)(void) \

void igt_simple_init(int argc, char **argv);
void igt_simple_init_parse_opts(int argc, char **argv,
				const char *extra_short_opts,
				struct option *extra_long_opts,
				const char *help_str,
				igt_opt_handler_t extra_opt_handler);

/**
 * igt_simple_main:
 *
 * This is a magic control flow block used instead of a main() function for
 * simple tests. Open-coding the main() function is only recommended if
 * the test needs to parse additional cmdline arguments of its own.
 */
#define igt_simple_main \
	static void igt_tokencat(__real_main, __LINE__)(void); \
	int main(int argc, char **argv) { \
		igt_simple_init(argc, argv); \
		igt_tokencat(__real_main, __LINE__)(); \
		exit(0); \
	} \
	static void igt_tokencat(__real_main, __LINE__)(void) \

__attribute__((format(printf, 1, 2)))
void igt_skip(const char *f, ...) __attribute__((noreturn));
__attribute__((format(printf, 5, 6)))
void __igt_skip_check(const char *file, const int line,
		      const char *func, const char *check,
		      const char *format, ...) __attribute__((noreturn));
void igt_success(void);

void igt_fail(int exitcode) __attribute__((noreturn));
__attribute__((format(printf, 6, 7)))
void __igt_fail_assert(int exitcode, const char *file,
		       const int line, const char *func, const char *assertion,
		       const char *format, ...)
	__attribute__((noreturn));
void igt_exit(void) __attribute__((noreturn));

/**
 * igt_assert:
 * @expr: condition to test
 *
 * Fails (sub-)test if the condition is not met.
 *
 * Should be used everywhere where a test checks results.
 */
#define igt_assert(expr) \
	do { if (!(expr)) \
		__igt_fail_assert(99, __FILE__, __LINE__, __func__, #expr , NULL); \
	} while (0)

/**
 * igt_assert_f:
 * @expr: condition to test
 * @...: format string and optional arguments
 *
 * Fails (sub-)test if the condition is not met.
 *
 * Should be used everywhere where a test checks results.
 *
 * In addition to the plain igt_assert() helper this allows to print additional
 * information to help debugging test failures.
 */
#define igt_assert_f(expr, f...) \
	do { if (!(expr)) \
		__igt_fail_assert(99, __FILE__, __LINE__, __func__, #expr , f); \
	} while (0)

/**
 * igt_fail_on:
 * @expr: condition to test
 *
 * Fails (sub-)test if the condition is met.
 *
 * Should be used everywhere where a test checks results.
 */
#define igt_fail_on(expr) igt_assert(!(expr))

/**
 * igt_assert_f:
 * @expr: condition to test
 * @...: format string and optional arguments
 *
 * Fails (sub-)test if the condition is met.
 *
 * Should be used everywhere where a test checks results.
 *
 * In addition to the plain igt_assert() helper this allows to print additional
 * information to help debugging test failures.
 */
#define igt_fail_on_f(expr, f...) igt_assert_f(!(expr), f)

/**
 * igt_assert_cmpint:
 * @n1: first value
 * @cmp: compare operator
 * @n2: second value
 *
 * Fails (sub-)test if the condition is not met
 *
 * Should be used everywhere where a test compares two integer values.
 *
 * Like igt_assert(), but displays the values being compared on failure instead
 * of simply printing the stringified expression.
 */
#define igt_assert_cmpint(n1, cmp, n2) \
	do { \
		int __n1 = (n1), __n2 = (n2); \
		if (__n1 cmp __n2) ; else \
		__igt_fail_assert(99, __FILE__, __LINE__, __func__, \
				  #n1 " " #cmp " " #n2, \
				  "error: %d %s %d\n", __n1, #cmp, __n2); \
	} while (0)

/**
 * igt_assert_eq:
 * @n1: first integer
 * @n2: second integer
 *
 * Fails (sub-)test if the two integers are not equal. Beware that for now this
 * only works on integers.
 *
 * Like igt_assert(), but displays the values being compared on failure instead
 * of simply printing the stringified expression.
 */
#define igt_assert_eq(n1, n2) igt_assert_cmpint(n1, ==, n2)

/**
 * igt_require:
 * @expr: condition to test
 *
 * Skip a (sub-)test if a condition is not met.
 *
 * Should be used everywhere where a test checks results to decide about
 * skipping. This is useful to streamline the skip logic since it allows for a more flat
 * code control flow, similar to igt_assert()
 */
#define igt_require(expr) \
	do { if (!(expr)) \
		__igt_skip_check(__FILE__, __LINE__, __func__, #expr , NULL); \
	} while (0)

/**
 * igt_skip_on:
 * @expr: condition to test
 *
 * Skip a (sub-)test if a condition is met.
 *
 * Should be used everywhere where a test checks results to decide about
 * skipping. This is useful to streamline the skip logic since it allows for a more flat
 * code control flow, similar to igt_assert()
 */
#define igt_skip_on(expr) \
	do { if ((expr)) \
		__igt_skip_check(__FILE__, __LINE__, __func__, "!(" #expr ")" , NULL); \
	} while (0)

/**
 * igt_require_f:
 * @expr: condition to test
 * @...: format string and optional arguments
 *
 * Skip a (sub-)test if a condition is not met.
 *
 * Should be used everywhere where a test checks results to decide about
 * skipping. This is useful to streamline the skip logic since it allows for a more flat
 * code control flow, similar to igt_assert()
 *
 * In addition to the plain igt_require() helper this allows to print additional
 * information to help debugging test failures.
 */
#define igt_require_f(expr, f...) igt_skip_on_f(!(expr), f)

/**
 * igt_skip_on_f:
 * @expr: condition to test
 * @...: format string and optional arguments
 *
 * Skip a (sub-)test if a condition is met.
 *
 * Should be used everywhere where a test checks results to decide about
 * skipping. This is useful to streamline the skip logic since it allows for a more flat
 * code control flow, similar to igt_assert()
 *
 * In addition to the plain igt_skip_on() helper this allows to print additional
 * information to help debugging test failures.
 */
#define igt_skip_on_f(expr, f...) \
	do { if ((expr)) \
		__igt_skip_check(__FILE__, __LINE__, __func__, #expr , f); \
	} while (0)

/* fork support code */
bool __igt_fork(void);

/**
 * igt_fork:
 * @child: name of the int variable with the child number
 * @num_children: number of children to fork
 *
 * This is a magic control flow block which spawns parallel test threads with
 * fork().
 *
 * The test children execute in parallel to the main test thread. Joining all
 * test threads should be done with igt_waitchildren to ensure that the exit
 * codes of all children are properly reflected in the test status.
 *
 * Note that igt_skip() will not be forwarded, feature tests need to be done
 * before spawning threads with igt_fork().
 */
#define igt_fork(child, num_children) \
	for (int child = 0; child < (num_children); child++) \
		for (; __igt_fork(); exit(0))
void igt_waitchildren(void);

/**
 * igt_helper_process_t:
 * @running: indicates whether the process is currently running
 * @use_SIGKILL: whether the helper should be terminated with SIGKILL or SIGTERM
 * @pid: pid of the helper if @running is true
 * @id: internal id
 *
 * Tracking structure for helper processes. Users of the i-g-t library should
 * only set @use_SIGKILL directly.
 */
struct igt_helper_process {
	bool running;
	bool use_SIGKILL;
	pid_t pid;
	int id;
};
bool __igt_fork_helper(struct igt_helper_process *proc);

/**
 * igt_fork_helper:
 * @proc: #igt_helper_process structure
 *
 * This is a magic control flow block which denotes an asynchronous helper
 * process block. The difference compared to igt_fork() is that failures from
 * the child process will not be forwarded, making this construct more suitable
 * for background processes. Common use cases are regular interference of the
 * main test thread through e.g. sending signals or evicting objects through
 * debugfs. Through the explicit #igt_helper_process they can also be controlled
 * in a more fine-grained way than test children spawned through igt_fork().
 *
 * For tests with subtest helper process can be started outside of a
 * #igt_subtest block.
 *
 * Calling igt_wait_helper() joins a helper process and igt_stop_helper()
 * forcefully terminates it.
 */
#define igt_fork_helper(proc) \
	for (; __igt_fork_helper(proc); exit(0))
int igt_wait_helper(struct igt_helper_process *proc);
void igt_stop_helper(struct igt_helper_process *proc);

/* exit handler code */

/**
 * igt_exit_handler_t:
 * @sig: Signal number which caused the exit or 0.
 *
 * Exit handler type used by igt_install_exit_handler(). Note that exit handlers
 * can potentially be run from signal handling contexts, the @sig parameter can
 * be used to figure this out and act accordingly.
 */
typedef void (*igt_exit_handler_t)(int sig);

/* reliable atexit helpers, also work when killed by a signal (if possible) */
void igt_install_exit_handler(igt_exit_handler_t fn);
void igt_enable_exit_handler(void);
void igt_disable_exit_handler(void);

/* helpers to automatically reduce test runtime in simulation */
bool igt_run_in_simulation(void);
/**
 * SLOW_QUICK:
 * @slow: value in simulation mode
 * @quick: value in normal mode
 *
 * Simple macro to select between two values (e.g. number of test rounds or test
 * buffer size) depending upon whether i-g-t is run in simulation mode or not.
 */
#define SLOW_QUICK(slow,quick) (igt_run_in_simulation() ? (quick) : (slow))

void igt_skip_on_simulation(void);

/* structured logging */
enum igt_log_level {
	IGT_LOG_DEBUG,
	IGT_LOG_INFO,
	IGT_LOG_WARN,
	IGT_LOG_NONE,
};
__attribute__((format(printf, 2, 3)))
void igt_log(enum igt_log_level level, const char *format, ...);
__attribute__((format(printf, 2, 0)))
void igt_vlog(enum igt_log_level level, const char *format, va_list args);

/**
 * igt_debug:
 * @...: format string and optional arguments
 *
 * Wrapper for igt_log() for message at the IGT_LOG_DEBUG level.
 */
#define igt_debug(f...) igt_log(IGT_LOG_DEBUG, f)

/**
 * igt_info:
 * @...: format string and optional arguments
 *
 * Wrapper for igt_log() for message at the IGT_LOG_INFO level.
 */
#define igt_info(f...) igt_log(IGT_LOG_INFO, f)

/**
 * igt_warn:
 * @...: format string and optional arguments
 *
 * Wrapper for igt_log() for message at the IGT_LOG_WARN level.
 */
#define igt_warn(f...) igt_log(IGT_LOG_WARN, f)
extern enum igt_log_level igt_log_level;

/**
 * igt_warn_on:
 * @condition: condition to test
 *
 * Print a IGT_LOG_WARN level message if a condition is not met.
 *
 * Should be used everywhere where a test checks results to decide about
 * printing warnings. This is useful to streamline the test logic since it
 * allows for a more flat code control flow, similar to igt_assert()
 */
#define igt_warn_on(condition) do {\
		if (condition) \
			igt_warn("Warning on condition %s in fucntion %s, file %s:%i\n", \
				 #condition, __func__, __FILE__, __LINE__); \
	} while (0)

/**
 * igt_warn_on_f:
 * @condition: condition to test
 * @...: format string and optional arguments
 *
 * Skip a (sub-)test if a condition is not met.
 *
 * Print a IGT_LOG_WARN level message if a condition is not met.
 *
 * Should be used everywhere where a test checks results to decide about
 * printing warnings. This is useful to streamline the test logic since it
 * allows for a more flat code control flow, similar to igt_assert()
 *
 * In addition to the plain igt_warn_on_f() helper this allows to print
 * additional information (again as warnings) to help debugging test failures.
 */
#define igt_warn_on_f(condition, f...) do {\
		if (condition) {\
			igt_warn("Warning on condition %s in fucntion %s, file %s:%i\n", \
				 #condition, __func__, __FILE__, __LINE__); \
			igt_warn(f); \
		} \
	} while (0)


void igt_set_timeout(unsigned int seconds);

#endif /* IGT_CORE_H */

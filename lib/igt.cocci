// Semantic patch for common patters and their replacement by igt infrastructure
// and macros. Please run with
//
// spatch --sp-file lib/igt.cocci --in-place tests/*.c
//
// on your new testcase.


// Replace open-coded augmented igt_assert/skip/require with macro versions
@@
expression Ec;
expression list[n] Ep;
@@
- if (Ec) {
(
- igt_warn( Ep );
|
- igt_info( Ep );
|
- igt_debug( Ep );
)
- igt_fail(...);
- }
+ igt_fail_on_f(Ec, Ep);
@@
expression Ec;
@@
- if (Ec) {
- igt_fail(...);
- }
+ igt_fail_on(Ec);
@@
expression Ec;
expression list[n] Ep;
@@
- if (Ec) {
- igt_skip(Ep);
- }
+ igt_skip_on_f(Ec, Ep);
@@
expression Ec;
expression list[n] Ep;
@@
- if (Ec) {
- igt_warn(Ep);
- }
+ igt_warn_on_f(Ec, Ep);

// Enforce use of logging functions
@@
expression list[n] Ep;
@@
-fprintf(stderr, Ep);
+igt_warn(Ep);
@@
expression E;
@@
-perror(E);
+igt_warn(E);
@@
expression list[n] Ep;
@@
-fprintf(stdout, Ep);
+igt_info(Ep);
@@
expression list[n] Ep;
@@
-printf(Ep);
+igt_info(Ep);

// No abort for tests, really. Should only be used for internal library checks
// in lib/*
@@
@@
-abort();
+igt_fail(IGT_EXIT_FAILURE);

@@
iterator name for_each_pipe;
igt_display_t *display;
expression pipe;
@@
- for (pipe = 0; pipe < igt_display_get_n_pipes(display); pipe++) {
+ for_each_pipe (display, pipe) {
...
}

// Tests really shouldn't use plain assert!
@@
expression E;
@@
- assert(E);
+ igt_assert(E);

// Replace open-coded igt_swap()
@@
type T;
T a, b, tmp;
@@
- tmp = a;
- a = b;
- b = tmp;
+ igt_swap(a, b);

// Replace open-coded min()
@@
expression a;
expression b;
@@
(
- ((a) < (b) ? (a) : (b))
+ min(a, b)
|
- ((a) <= (b) ? (a) : (b))
+ min(a, b)
)

// Replace open-coded max()
@@
expression a;
expression b;
@@
(
- ((a) > (b) ? (a) : (b))
+ max(a, b)
|
- ((a) >= (b) ? (a) : (b))
+ max(a, b)
)

// drm_open_any always returns a valid file descriptor
@@
expression a;
@@
a = drm_open_any();
(
- igt_assert(a >= 0);
|
- if (a < 0) {
- ...
- return ...;
- }
)

// Use comparison macros instead of raw igt_assert when possible
@@
typedef uint32_t;
uint32_t E1, E2;
int E3, E4;
@@
(
- igt_assert(E1 == E2);
+ igt_assert_eq_u32(E1, E2);
|
- igt_assert(E1 != E2);
+ igt_assert_neq_u32(E1, E2);
|
- igt_assert(E1 <= E2);
+ igt_assert_lte_u32(E1, E2);
|
- igt_assert(E1 < E2);
+ igt_assert_lt_u32(E1, E2);
|
- igt_assert(E1 >= E2);
+ igt_assert_lte_u32(E2, E1);
|
- igt_assert(E1 > E2);
+ igt_assert_lt_u32(E2, E1);
|
- igt_assert(E3 == E4);
+ igt_assert_eq(E3, E4);
|
- igt_assert(E3 != E4);
+ igt_assert_neq(E3, E4);
|
- igt_assert(E3 <= E4);
+ igt_assert_lte(E3, E4);
|
- igt_assert(E3 < E4);
+ igt_assert_lt(E3, E4);
|
- igt_assert(E3 >= E4);
+ igt_assert_lte(E4, E3);
|
- igt_assert(E3 > E4);
+ igt_assert_lt(E4, E3);
)

// avoid unused-result warnings when compiling with _FORTIFY_SOURCE defined
@@
identifier func =~ "^(read|write)$";
expression list[2] E;
expression size;
@@
-func(E, size);
+igt_assert_eq(func(E, size), size);

@@
expression ptr, size, nmemb, stream;
@@
-fread(ptr, size, nmemb, stream);
+igt_assert_eq(fread(ptr, size, nmemb, stream), nmemb);

@@
expression list E;
@@
-fgets(E);
+igt_assert(fgets(E) != NULL);

@@
identifier func =~ "^v?asprintf$";
expression list E;
@@
-func(E);
+igt_assert_neq(func(E), -1);

// replace open-coded do_ioctl
@@
expression a, b, c, e;
@@
(
-do_or_die(drmIoctl(a, b, c));
+do_ioctl(a, b, c);
|
-igt_assert(drmIoctl(a, b, c) == 0);
+do_ioctl(a, b, c);
|
-igt_assert(drmIoctl(a, b, c) == -1 && errno == e);
+do_ioctl_err(a, b, c, e);
|
-igt_assert(drmIoctl(a, b, c) < 0 && errno == e);
+do_ioctl_err(a, b, c, e);
)

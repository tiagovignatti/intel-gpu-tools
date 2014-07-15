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
+igt_fail(1);

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

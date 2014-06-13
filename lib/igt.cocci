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

// Replace open-coded augmented igt_assert/skip/require with macro versions
@@
expression Ec;
expression list[n] Ep;
@@
- if (Ec) {
- fprintf( stderr,  Ep );
- igt_fail(...);
- }
+ igt_assert_f(Ec, Ep);
@@
expression Ec;
expression list[n] Ep;
@@
- if (Ec) {
- igt_skip(Ep);
- }
+ igt_skip_on_f(Ec, Ep);

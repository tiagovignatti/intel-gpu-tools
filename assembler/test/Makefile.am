check_SCRIPTS = run-test.sh

TESTS_ENVIRONMENT = top_builddir=${top_builddir}
TESTS = \
	mov

TESTDATA = \
	mov.expected \
	mov.g4a

EXTRA_DIST = \
	${TESTDATA} \
	run-test.sh

$(TESTS): run-test.sh
	sed "s|TEST|$@|g" ${srcdir}/run-test.sh > $@
	chmod +x $@

CLEANFILES = \
	*.out \
	${TESTS}
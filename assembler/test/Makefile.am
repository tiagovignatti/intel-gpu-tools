check_SCRIPTS = run-test.sh

TESTS_ENVIRONMENT = top_builddir=${top_builddir}
TESTS = \
	mov \
	frc \
	rndd \
	rndu \
	rnde-intsrc \
	rndz \
	lzd \
	not

# Tests that are expected to fail because they contain some inccorect code.
XFAIL_TESTS = \
	rnde-intsrc

TESTDATA = \
	mov.expected \
	mov.g4a \
	frc.expected \
	frc.g4a \
	rndd.expected \
	rndd.g4a \
	rndu.expected \
	rndu.g4a \
	rnde.expected \
	rnde.g4a \
	rnde-intsrc.expected \
	rnde-intsrc.g4a \
	rndz.expected \
	rndz.g4a \
	lzd.expected \
	lzd.g4a \
	not.expected \
	not.g4a

EXTRA_DIST = \
	${TESTDATA} \
	run-test.sh

$(TESTS): run-test.sh
	sed "s|TEST|$@|g" ${srcdir}/run-test.sh > $@
	chmod +x $@

CLEANFILES = \
	*.out \
	${TESTS}
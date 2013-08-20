check_SCRIPTS = run-test.sh

TESTS_ENVIRONMENT = top_builddir=${top_builddir}
TESTS = \
	mov \
	frc \
	rndd \
	rndu \
	rnde \
	rnde-intsrc \
	rndz \
	lzd \
	not \
	jmpi \
	if \
	iff \
	while \
	else \
	break \
	cont \
	halt \
	wait \
	endif \
	declare \
	immediate

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
	not.g4a \
	jmpi.expected \
	jmpi.g4a \
	if.expected \
	if.g4a \
	iff.expected \
	iff.g4a \
	while.expected \
	while.g4a \
	else.expected \
	else.g4a \
	break.expected \
	break.g4a \
	cont.expected \
	cont.g4a \
	halt.expected \
	halt.g4a \
	wait.expected \
	wait.g4a \
	endif.expected \
	endif.g4a \
	declare.expected \
	declare.g4a \
	immediate.g4a \
	immediate.expected

EXTRA_DIST = \
	${TESTDATA} \
	run-test.sh

$(TESTS): run-test.sh
	sed "s|TEST|$@|g" ${srcdir}/run-test.sh > $@
	chmod +x $@

CLEANFILES = \
	*.out \
	${TESTS}

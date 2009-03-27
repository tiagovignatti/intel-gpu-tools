TESTS = auth \
	openclose \
	getversion \
	getclient \
	getstats \
	lock \
	setversion \
	updatedraw \
	gem_basic \
	gem_flink \
	gem_readwrite \
	gem_mmap

EXTRA_LTLIBRARIES = libdrmtest.la
EXTRA_LTLIBRARIES = libdrmtest.la
libdrmtest_la_SOURCES = \
	drmtest.c \
	drmtest.h
libdrmtest_la_LIBADD = \
	$(top_builddir)/libdrm/libdrm.la

EXTRA_PROGRAMS = $(TESTS)
CLEANFILES = $(EXTRA_PROGRAMS) $(EXTRA_LTLIBRARIES)

# Copyright © 2005 Adam Jackson.
# Copyright © 2009,2013 Intel Corporation
#
#  Permission is hereby granted, free of charge, to any person obtaining a
#  copy of this software and associated documentation files (the "Software"),
#  to deal in the Software without restriction, including without limitation
#  on the rights to use, copy, modify, merge, publish, distribute, sub
#  license, and/or sell copies of the Software, and to permit persons to whom
#  the Software is furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice (including the next
#  paragraph) shall be included in all copies or substantial portions of the
#  Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
#  ADAM JACKSON BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
#  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
#  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

ACLOCAL_AMFLAGS = ${ACLOCAL_FLAGS} -I m4

SUBDIRS = docs lib man tools scripts benchmarks demos overlay

if BUILD_ASSEMBLER
SUBDIRS += assembler
endif

if BUILD_SHADER_DEBUGGER
SUBDIRS += debugger
endif

if BUILD_TESTS
SUBDIRS += tests
endif

MAINTAINERCLEANFILES = ChangeLog INSTALL

.PHONY: version.h.tmp
version.h.tmp:
	@touch $@
	@if test -d .git; then \
		if which git > /dev/null; then git log -n 1 --oneline | \
			sed 's/^\([^ ]*\) .*/#define IGT_GIT_SHA1 "g\1"/' \
			>> $@ ; \
		fi \
	else \
		echo '#define IGT_GIT_SHA1 "NOT-GIT"' >> $@ ; \
	fi

version.h: version.h.tmp
	@echo "updating version.h"
	@if ! cmp -s version.h.tmp version.h; then \
		mv version.h.tmp version.h ;\
	else \
		rm version.h.tmp ;\
	fi

BUILT_SOURCES = version.h
CLEANFILES = version.h version.h.tmp

.PHONY: ChangeLog INSTALL

INSTALL:
	$(INSTALL_CMD)

ChangeLog:
	$(CHANGELOG_CMD)

dist-hook: ChangeLog INSTALL

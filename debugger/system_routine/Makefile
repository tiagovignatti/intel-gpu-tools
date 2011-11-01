# Copyright © 2011 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
#
# Authors:
#    Ben Widawsky <ben@bwidawsk.net>

OBJCOPY?=objcopy

PRECPP=./pre_cpp.py
CPP_FLAGS=-x assembler-with-cpp -P

GEN4ASM_FLAGS?=-g6 -a -b

TEMP:=$(shell mktemp)
TEMP2:=$(shell mktemp)

GPU?=SANDYBRIDGE
DEFINES+=-DGEN_ASM -D$(GPU) -I. -I../..//lib

all: sr

sr.c: sr.g4a eviction_macro evict.h
	$(PRECPP) $^ > $(TEMP)
	$(CPP) $(CPP_FLAGS) $(DEFINES) -o $(TEMP2) $(TEMP)
	$(GEN4ASM) $(GEN4ASM_FLAGS) $(TEMP2) -o $@

sr : sr.o
	$(OBJCOPY) -O binary -K gen_eu_bytes $^ $@

# Test files are useful for development. The system routine kernel is very
# simple and doesn't depend on any external communication to run.
test.c: test.g4a
	$(PRECPP) $^ > $(TEMP)
	$(CPP) $(CPP_FLAGS) $(DEFINES) -o $(TEMP2) $(TEMP)
	$(GEN4ASM) $(GEN4ASM_FLAGS) $(TEMP2) -o $@

test : test.o
	$(OBJCOPY) -O binary -K gen_eu_bytes $^ $@

helper: sr.g4a eviction_macro
	$(PRECPP) $^ > help
	$(CPP) $(CPP_FLAGS) $(DEFINES) -o help2 help
	$(GEN4ASM) $(GEN4ASM_FLAGS) help2 -o $@

eviction_macro :

evict.h : eviction_macro
	$(shell ./eviction_macro >| evict.h)

.PHONY : clean
clean :
	$(RM) *.o sr.c sr test.c test evict.h eviction_macro

.PHONY : clean
distclean: clean
	$(RM) help* GNUmakefile

maintainer-clean: clean

EMPTY_AUTOMAKE_TARGETS = install install-data install-exec uninstall \
			 install-dvi install-html install-info install-ps \
			 install-pdf installdirs check installcheck \
			 mostlyclean dvi pdf ps info html tags ctags \
			 distdir
.PHONY: $(EMPTY_AUTOMAKE_TARGETS)
$(EMPTY_AUTOMAKE_TARGETS):

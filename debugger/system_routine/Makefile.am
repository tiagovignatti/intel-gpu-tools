
noinst_PROGRAMS = eviction_macro sr tiny
nodist_sr_SOURCES = sr.c
nodist_tiny_SOURCES = tiny.c

GEN4ASM = $(top_builddir)/assembler/intel-gen4asm
GEN4ASM_FLAGS = -g6 -a -b
ASM_CPPFLAGS = 				\
	-x assembler-with-cpp 		\
	-P -DGEN_ASM -DSANDYBRIDGE 	\
	-I$(top_srcdir)/lib 		\
	-I$(builddir)

evict.h : eviction_macro
	$(builddir)/eviction_macro > evict.h

sr.cpp : sr.g4a
	$(srcdir)/pre_cpp.py $(srcdir)/sr.g4a > $@.tmp && mv $@.tmp $@
sr.asm : sr.cpp evict.h
	$(CPP) $(ASM_CPPFLAGS) -o $@ sr.cpp
sr.c: sr.asm
	$(GEN4ASM) $(GEN4ASM_FLAGS) sr.asm -o $@
sr.o : sr.c
	$(CC) -c -o $@ sr.c
sr$(EXEEXT) : sr.o
	$(OBJCOPY) -O binary -K gen_eu_bytes sr.o $@

# Test.g4a is the simplest possible system routine we can run on the GPU
# without actually hanging the system. The system routine kernel is very
# simple and doesn't depend on any external communication to run.
tiny.cpp : test.g4a
	$(srcdir)/pre_cpp.py $(srcdir)/test.g4a > $@.tmp && mv $@.tmp $@
tiny.asm : tiny.cpp
	$(CPP) $(ASM_CPPFLAGS) -o $@ tiny.cpp
tiny.c: tiny.asm
	$(GEN4ASM) $(GEN4ASM_FLAGS) tiny.asm -o $@
tiny.o : tiny.c
	$(CC) -c -o $@ tiny.c
tiny$(EXEEXT) : tiny.o
	$(OBJCOPY) -O binary -K gen_eu_bytes tiny.o $@

CLEANFILES = evict.h sr.cpp sr.asm sr.c tiny.cpp tiny.asm tiny.c
EXTRA_DIST = pre_cpp.py sr.g4a test.g4a

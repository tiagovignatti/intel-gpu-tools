AM_YFLAGS = -d

AM_CFLAGS= $(WARN_CFLAGS)
bin_PROGRAMS = intel-gen4asm

intel_gen4asm_SOURCES = \
	brw_defines.h \
	brw_structs.h \
	gen4asm.h \
	gram.y \
	lex.l \
	main.c

gram.h: gram.c

BUILT_SOURCES = gram.h gram.c lex.c
MAINTAINERCLEANFILES = $(BUILT_SOURCES)

# man_MANS = intel-gen4asm.1

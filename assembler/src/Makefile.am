AM_YFLAGS = -d

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

# man_MANS = gen4asm.1

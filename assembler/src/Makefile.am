AM_YFLAGS = -d --warnings=all

AM_CFLAGS= $(WARN_CFLAGS)
bin_PROGRAMS = intel-gen4asm intel-gen4disasm

intel_gen4asm_SOURCES = \
	brw_defines.h \
	brw_structs.h \
	gen4asm.h \
	gram.y \
	lex.l \
	main.c

intel_gen4disasm_SOURCES =  \
	disasm.c disasm-main.c

gram.h: gram.c

BUILT_SOURCES = gram.h gram.c lex.c
MAINTAINERCLEANFILES = $(BUILT_SOURCES)
LEX = flex -i

# man_MANS = intel-gen4asm.1

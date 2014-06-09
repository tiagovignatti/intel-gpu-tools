AM_CPPFLAGS = -I$(top_srcdir)

noinst_PROGRAMS = intel_null_state_gen

intel_null_state_gen_SOURCES = 	\
	intel_batchbuffer.c \
	intel_batchbuffer.h \
	intel_renderstate_gen6.c \
	intel_renderstate_gen7.c \
	intel_renderstate_gen8.c \
	intel_null_state_gen.c

gens := 6 7 8

h = /tmp/intel_renderstate_gen$$gen.c
state_headers: intel_null_state_gen
	for gen in $(gens); do \
		./intel_null_state_gen $$gen >$(h) ;\
	done

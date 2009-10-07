bin_PROGRAMS = \
	intel_gpu_dump \
	intel_gpu_top \
	intel_stepping \
	intel_reg_dumper \
	intel_reg_write

intel_gpu_dump_SOURCES = \
	intel_gpu_dump.c \
	intel_decode.h

LDADD = ../lib/libintel_tools.la $(DRM_LIBS) $(PCIACCESS_LIBS)

AM_CFLAGS = $(DRM_CFLAGS) $(PCIACCESS_CFLAGS) $(WARN_CFLAGS) \
	-I$(srcdir)/.. \
	-I$(srcdir)/../lib

bin_PROGRAMS = \
	intel_audio_dump \
	intel_bios_dumper \
	intel_bios_reader \
	intel_error_decode \
	intel_gpu_dump \
	intel_gpu_top \
	intel_gpu_time \
	intel_gtt \
	intel_lid \
	intel_stepping \
	intel_reg_dumper \
	intel_reg_snapshot \
	intel_reg_write \
	intel_reg_read

intel_gpu_dump_SOURCES = \
	intel_gpu_dump.c \
	intel_decode.c \
	intel_decode.h

intel_error_decode_SOURCES = \
	intel_error_decode.c \
	intel_decode.c \
	intel_decode.h

intel_bios_reader_SOURCES = \
	intel_bios_reader.c \
	intel_bios.h

LDADD = ../lib/libintel_tools.la $(DRM_LIBS) $(PCIACCESS_LIBS)

AM_CFLAGS = $(DRM_CFLAGS) $(PCIACCESS_CFLAGS) $(WARN_CFLAGS) \
	-I$(srcdir)/.. \
	-I$(srcdir)/../lib

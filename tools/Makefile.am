bin_PROGRAMS = \
	intel_disable_clock_gating \
	intel_audio_dump \
	intel_backlight \
	intel_bios_dumper \
	intel_bios_reader \
	intel_error_decode \
	intel_gpu_top \
	intel_gpu_time \
	intel_gtt \
	intel_stepping \
	intel_reg_dumper \
	intel_reg_snapshot \
	intel_reg_write \
	intel_reg_read \
	forcewaked \
	$(NULL)

noinst_PROGRAMS = \
	intel_dump_decode \
	intel_lid \
	$(NULL)

intel_dump_decode_SOURCES = \
	intel_dump_decode.c \
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

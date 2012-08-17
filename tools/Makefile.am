bin_PROGRAMS = 				\
	intel_disable_clock_gating 	\
	intel_audio_dump 		\
	intel_backlight 		\
	intel_bios_dumper 		\
	intel_bios_reader 		\
	intel_error_decode 		\
	intel_gpu_top 			\
	intel_gpu_time 			\
	intel_gtt 			\
	intel_stepping 			\
	intel_reg_checker 		\
	intel_reg_dumper 		\
	intel_reg_snapshot 		\
	intel_reg_write 		\
	intel_reg_read 			\
	intel_forcewaked		\
	intel_dpio_read			\
	intel_dpio_write		\
	intel_l3_parity

noinst_PROGRAMS = 			\
	intel_dump_decode 		\
	intel_infoframes		\
	intel_lid			\
	intel_panel_fitter

dist_bin_SCRIPTS = intel_gpu_abrt

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/lib
AM_CFLAGS = $(DRM_CFLAGS) $(PCIACCESS_CFLAGS) $(CWARNFLAGS) $(CAIRO_CFLAGS)
LDADD = $(top_builddir)/lib/libintel_tools.la $(DRM_LIBS) $(PCIACCESS_LIBS) $(CAIRO_LIBS)

intel_dump_decode_SOURCES = 	\
	intel_dump_decode.c

intel_error_decode_SOURCES =	\
	intel_error_decode.c

intel_bios_reader_SOURCES =	\
	intel_bios_reader.c	\
	intel_bios.h

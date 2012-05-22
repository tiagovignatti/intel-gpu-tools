
bin_PROGRAMS = 				\
	intel_upload_blit_large		\
	intel_upload_blit_large_gtt	\
	intel_upload_blit_large_map	\
	intel_upload_blit_small

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/lib
AM_CFLAGS = $(DRM_CFLAGS) $(CWARNFLAGS) $(CAIRO_CFLAGS)
LDADD = $(top_builddir)/lib/libintel_tools.la $(DRM_LIBS) $(PCIACCESS_LIBS) $(CAIRO_LIBS)

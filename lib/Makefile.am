
noinst_LTLIBRARIES = libintel_tools.la

AM_CPPFLAGS = -I$(top_srcdir)
AM_CFLAGS = $(DRM_CFLAGS) $(CWARNFLAGS)

libintel_tools_la_SOURCES = 	\
	debug.h			\
	drmtest.c		\
	drmtest.h		\
	i830_reg.h		\
	i915_3d.h		\
	i915_reg.h		\
	instdone.c		\
	instdone.h		\
	intel_batchbuffer.c	\
	intel_batchbuffer.h	\
	intel_chipset.h		\
	intel_drm.c		\
	intel_gpu_tools.h	\
	intel_mmio.c		\
	intel_pci.c		\
	intel_reg.h		\
	rendercopy_i915.c	\
	rendercopy_i830.c	\
	gen6_render.h		\
	gen7_render.h		\
	rendercopy_gen6.c	\
	rendercopy_gen7.c	\
	rendercopy.h		\
	intel_reg_map.c		\
	intel_dpio.c		\
	$(NULL)

LDADD = $(CAIRO_LIBS)
AM_CFLAGS += $(CAIRO_CFLAGS)

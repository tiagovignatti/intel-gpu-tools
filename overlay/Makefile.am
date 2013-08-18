if BUILD_OVERLAY
bin_PROGRAMS = intel-gpu-overlay
endif

AM_CPPFLAGS = -I.
AM_CFLAGS = $(DRM_CFLAGS) $(PCIACCESS_CFLAGS) $(CWARNFLAGS) $(CAIRO_CFLAGS) $(OVERLAY_CFLAGS)
LDADD = $(DRM_LIBS) $(PCIACCESS_LIBS) $(CAIRO_LIBS) $(OVERLAY_LIBS)

intel_gpu_overlay_SOURCES = \
	i915_pciids.h \
	overlay.h \
	overlay.c \
	chart.h \
	chart.c \
	cpu-top.h \
	cpu-top.c \
	gem-objects.h \
	gem-objects.c \
	gpu-top.h \
	gpu-top.c \
	gpu-perf.c \
	gpu-freq.h \
	gpu-freq.c \
	igfx.h \
	igfx.c \
	x11/dri2.c \
	x11/dri2.h \
	x11/rgb2yuv.c \
	x11/rgb2yuv.h \
	x11/x11-overlay.c \
	$(NULL)

EXTRA_DIST=README

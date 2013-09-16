if BUILD_OVERLAY
bin_PROGRAMS = intel-gpu-overlay
endif

AM_CPPFLAGS = -I.
AM_CFLAGS = $(DRM_CFLAGS) $(PCIACCESS_CFLAGS) $(CWARNFLAGS) $(CAIRO_CFLAGS) $(OVERLAY_CFLAGS)
LDADD = $(DRM_LIBS) $(PCIACCESS_LIBS) $(CAIRO_LIBS) $(OVERLAY_LIBS)

intel_gpu_overlay_SOURCES = \
	i915_pciids.h \
	chart.h \
	chart.c \
	config.c \
	cpu-top.h \
	cpu-top.c \
	debugfs.h \
	debugfs.c \
	gem-interrupts.h \
	gem-interrupts.c \
	gem-objects.h \
	gem-objects.c \
	gpu-top.h \
	gpu-top.c \
	gpu-perf.h \
	gpu-perf.c \
	gpu-freq.h \
	gpu-freq.c \
	igfx.h \
	igfx.c \
	overlay.h \
	overlay.c \
	perf.h \
	perf.c \
	power.h \
	power.c \
	rc6.h \
	rc6.c \
	$(NULL)

if BUILD_OVERLAY_XLIB
both_x11_sources = x11/position.c x11/position.h
AM_CFLAGS += $(OVERLAY_XLIB_CFLAGS) $(XRANDR_CFLAGS)
LDADD += $(OVERLAY_XLIB_LIBS) $(XRANDR_LIBS)
intel_gpu_overlay_SOURCES += \
	x11/x11-window.c \
	$(NULL)
endif

if BUILD_OVERLAY_XVLIB
both_x11_sources = x11/position.c
AM_CFLAGS += $(OVERLAY_XVLIB_CFLAGS) $(XRANDR_CFLAGS)
LDADD += $(OVERLAY_XVLIB_LIBS) $(XRANDR_LIBS)
intel_gpu_overlay_SOURCES += \
	x11/dri2.c \
	x11/dri2.h \
	x11/rgb2yuv.c \
	x11/rgb2yuv.h \
	x11/x11-overlay.c \
	$(NULL)
endif

intel_gpu_overlay_SOURCES += \
	kms/kms-overlay.c \
	$(NULL)

intel_gpu_overlay_SOURCES += $(both_x11_sources)

intel_gpu_overlay_LDADD = $(LDADD) -lrt

EXTRA_DIST=README

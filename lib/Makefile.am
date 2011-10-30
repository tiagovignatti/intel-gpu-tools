NULL=#

AM_CFLAGS = $(WARN_CFLAGS) -I$(srcdir)/..
libintel_tools_la_SOURCES = \
	intel_batchbuffer.h \
	intel_chipset.h \
	intel_gpu_tools.h \
	intel_mmio.c \
	intel_pci.c \
	intel_reg.h \
	i915_3d.h \
	i915_reg.h \
	i830_reg.h \
	intel_reg_map.c \
	instdone.c \
	instdone.h \
	drmtest.h

if HAVE_DRM
libintel_tools_la_SOURCES += \
	intel_batchbuffer.c \
	intel_drm.c \
	drmtest.c \
	$(NULL)
AM_CFLAGS += $(DRM_CFLAGS)
endif


noinst_LTLIBRARIES = libintel_tools.la


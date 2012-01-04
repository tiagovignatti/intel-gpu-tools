NULL=#

AM_CFLAGS = $(CWARNFLAGS) -I$(srcdir)/..
libintel_tools_la_SOURCES = \
	intel_batchbuffer.h \
	intel_batchbuffer.c \
	intel_chipset.h \
	intel_gpu_tools.h \
	intel_mmio.c \
	intel_pci.c \
	intel_reg.h \
	i915_3d.h \
	i915_reg.h \
	i830_reg.h \
	intel_reg_map.c \
	intel_drm.c \
	instdone.c \
	instdone.h \
	drmtest.h \
	drmtest.c \
	debug.h \
	$(NULL)

AM_CFLAGS += $(DRM_CFLAGS)


noinst_LTLIBRARIES = libintel_tools.la


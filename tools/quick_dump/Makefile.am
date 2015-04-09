AM_CPPFLAGS = -I$(top_srcdir)/lib -I$(top_builddir)/lib $(PYTHON_CPPFLAGS) $(DRM_CFLAGS) $(CAIRO_CFLAGS) -I$(top_srcdir)

dist_bin_SCRIPTS = quick_dump.py reg_access.py
bin_SCRIPTS = chipset.py

lib_LTLIBRARIES = I915ChipsetPython.la
I915ChipsetPython_la_LDFLAGS = -module -avoid-version $(PYTHON_LDFLAGS) \
	-Wl,--no-undefined
I915ChipsetPython_la_SOURCES = chipset_macro_wrap.c
nodist_I915ChipsetPython_la_SOURCES = chipset_wrap_python.c
I915ChipsetPython_la_LIBADD =			\
	$(top_builddir)/lib/libintel_tools.la	\
	$(PCIACCESS_LIBS)			\
	$(DRM_LIBS)				\
	$(CAIRO_LIBS)				\
	$(LIBUNWIND_LIBS)			\
	$(NULL)

chipset.py: chipset_wrap_python.c

chipset_wrap_python.c: chipset.i
	$(AM_V_GEN)$(SWIG) $(AX_SWIG_PYTHON_OPT) -I/usr/include -I$(top_srcdir)/lib -o $@ $<

all-local: I915ChipsetPython.la
	$(LN_S) -f .libs/I915ChipsetPython.so _chipset.so

CLEANFILES = chipset_wrap_python.c chipset.py _chipset.so
EXTRA_DIST = $(QUICK_DUMP_EXTRA_DIST) \
	      base_interrupt.txt base_other.txt base_power.txt base_rings.txt \
	      quick_dump.py \
	      reg_access.py \
	      chipset.i

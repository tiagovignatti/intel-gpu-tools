BUILT_SOURCES = chipset_wrap_python.c

bin_SCRIPTS = quick_dump.py chipset.py reg_access.py

lib_LTLIBRARIES = I915ChipsetPython.la
I915ChipsetPython_la_CFLAGS = -I$(top_srcdir)/lib $(PYTHON_CPPFLAGS) $(CFLAGS) -I/usr/include/libdrm/
I915ChipsetPython_la_LDFLAGS = -module -avoid-version $(PYTHON_LDFLAGS) -lpciaccess
I915ChipsetPython_la_SOURCES = chipset_wrap_python.c intel_chipset.c \
			       $(top_srcdir)/lib/intel_drm.c  \
			       $(top_srcdir)/lib/intel_pci.c  \
			       $(top_srcdir)/lib/intel_reg_map.c  \
			       $(top_srcdir)/lib/intel_mmio.c

chipset_wrap_python.c: chipset.i
	$(SWIG) $(AX_SWIG_PYTHON_OPT) -I/usr/include -I$(top_srcdir)/lib -o $@ $<

all-local: I915ChipsetPython.la
	$(LN_S) -f .libs/I915ChipsetPython.so _chipset.so

CLEANFILES = chipset_wrap_python.c chipset.py _chipset.so
EXTRA_DIST =  \
	      base_display.txt base_interrupt.txt base_other.txt base_power.txt base_rings.txt \
	      gen6_other.txt sandybridge \
	      gen7_other.txt ivybridge \
	      vlv_display.txt valleyview \
	      quick_dump.py \
	      reg_access.py \
	      chipset.i chipset.py

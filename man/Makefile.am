appmandir = $(APP_MAN_DIR)
appman_RST = 				\
	intel_aubdump.rst		\
	intel_audio_dump.rst		\
	intel_bios_dumper.rst		\
	intel_bios_reader.rst		\
	intel_error_decode.rst		\
	intel_gpu_frequency.rst		\
	intel_gpu_top.rst		\
	intel_gtt.rst			\
	intel_infoframes.rst		\
	intel_lid.rst			\
	intel_panel_fitter.rst		\
	intel_reg.rst			\
	intel_stepping.rst		\
	intel_upload_blit_large.rst	\
	intel_upload_blit_large_gtt.rst \
	intel_upload_blit_large_map.rst \
	intel_upload_blit_small.rst

if HAVE_RST2MAN
appman_DATA = $(appman_RST:rst=$(APP_MAN_SUFFIX))
endif

EXTRA_DIST = $(appman_RST)
CLEANFILES = $(appman_DATA) defs.rst

SUFFIXES = .$(APP_MAN_SUFFIX)

# create some definitions useful in the man page rst sources
defs.rst:
	$(AM_V_GEN)echo ".. |PACKAGE_NAME| replace:: $(PACKAGE_NAME)" > $@
	$(AM_V_GEN)echo ".. |PACKAGE_VERSION| replace:: $(PACKAGE_VERSION)" >> $@
	$(AM_V_GEN)echo ".. |PACKAGE_STRING| replace:: $(PACKAGE_STRING)" >> $@
	$(AM_V_GEN)echo ".. |MANUAL_SECTION| replace:: 1" >> $@
	$(AM_V_GEN)echo ".. |MANUAL_GROUP| replace:: General Commands Manual" >> $@

%.$(APP_MAN_SUFFIX): %.rst defs.rst
	$(AM_V_GEN)rst2man < $< > $@

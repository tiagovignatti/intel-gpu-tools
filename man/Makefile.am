appmandir = $(APP_MAN_DIR)
appman_PRE = 				\
	intel_audio_dump.man		\
	intel_bios_dumper.man		\
	intel_bios_reader.man		\
	intel_error_decode.man		\
	intel_gpu_top.man		\
	intel_gtt.man			\
	intel_infoframes.man		\
	intel_lid.man			\
	intel_panel_fitter.man		\
	intel_reg_dumper.man		\
	intel_reg_read.man		\
	intel_reg_write.man		\
	intel_stepping.man		\
	intel_upload_blit_large.man	\
	intel_upload_blit_large_gtt.man \
	intel_upload_blit_large_map.man \
	intel_upload_blit_small.man

appman_DATA = $(appman_PRE:man=$(APP_MAN_SUFFIX))

EXTRA_DIST = $(appman_PRE)
CLEANFILES = $(appman_DATA)

# String replacements in MAN_SUBSTS now come from xorg-macros.m4 via configure
SUFFIXES = .$(APP_MAN_SUFFIX) .man

.man.$(APP_MAN_SUFFIX):
	$(AM_V_GEN)$(SED) $(MAN_SUBSTS) < $< > $@

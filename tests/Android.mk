include $(LOCAL_PATH)/tests/Makefile.sources
include $(LOCAL_PATH)/lib/Makefile.sources

LIBPCIACCESS_PATH := $(firstword $(wildcard  \
   $(TOP)/external/PRIVATE/libpciaccess      \
   $(TOP)/hardware/intel/libpciaccess        \
   $(TOP)/external/libpciaccess))
ifeq ($(LIBPCIACCESS_PATH),)
   $(error "Unable to find libpciaccess!")
endif

LIBDRM_PATH := $(firstword $(wildcard  \
   $(TOP)/external/PRIVATE/drm         \
   $(TOP)/external/drm))
ifeq ($(LIBDRM_PATH),)
   $(error "Unable to find libdrm!")
endif

skip_lib_list := \
    igt_kms.c \
    igt_kms.h

lib_list := $(filter-out $(skip_lib_list),$(libintel_tools_la_SOURCES))
LIB_SOURCES := $(addprefix lib/,$(lib_list))

.PHONY: version.h.tmp

$(LOCAL_PATH)/version.h.tmp:
	@touch $@
	@if test -d .git; then \
		if which git > /dev/null; then git log -n 1 --oneline | \
		        sed 's/^\([^ ]*\) .*/#define IGT_GIT_SHA1 "g\1"/' \
		        >> $@ ; \
		fi \
	else \
		echo '#define IGT_GIT_SHA1 "NOT-GIT"' >> $@ ; \
	fi

$(LOCAL_PATH)/version.h: $(LOCAL_PATH)/version.h.tmp
	@echo "updating version.h"
	@if ! cmp -s $(LOCAL_PATH)/version.h.tmp $(LOCAL_PATH)/version.h; then \
		mv $(LOCAL_PATH)/version.h.tmp $(LOCAL_PATH)/version.h ; \
	else \
		rm $(LOCAL_PATH)/version.h.tmp ; \
	fi

# FIXME: autogenerate this info #
$(LOCAL_PATH)/config.h:
	@echo "updating config.h"
	echo '#define PACKAGE_VERSION "1.5"' >> $@ ; \
	echo '#define TARGET_CPU_PLATFORM "android-ia"' >> $@ ;

#================#

define add_test
    include $(CLEAR_VARS)

    LOCAL_SRC_FILES :=          \
       tests/$1.c               \
       $(LIB_SOURCES)

    LOCAL_GENERATED_SOURCES :=       \
       $(LOCAL_PATH)/version.h       \
       $(LOCAL_PATH)/config.h

    LOCAL_C_INCLUDES +=              \
       $(LOCAL_PATH)/lib             \
       $(LIBDRM_PATH)/include/drm    \
       $(LIBDRM_PATH)/intel          \
       $(LIBPCIACCESS_PATH)/include

    LOCAL_CFLAGS += -DHAVE_STRUCT_SYSINFO_TOTALRAM
    LOCAL_CFLAGS += -DANDROID -UNDEBUG -include "check-ndebug.h"
    LOCAL_CFLAGS += -std=c99
    # FIXME: drop once Bionic correctly annotates "noreturn" on pthread_exit
    LOCAL_CFLAGS += -Wno-error=return-type
    # Excessive complaining for established cases. Rely on the Linux version warnings.
    LOCAL_CFLAGS += -Wno-sign-compare

    LOCAL_MODULE := $1
    LOCAL_MODULE_TAGS := optional

    LOCAL_SHARED_LIBRARIES := libpciaccess  \
                              libdrm        \
                              libdrm_intel

    include $(BUILD_EXECUTABLE)
endef

#================#

skip_tests_list := \
    testdisplay \
    kms_addfb \
    kms_cursor_crc \
    kms_flip \
    kms_pipe_crc_basic \
    kms_fbc_crc \
    kms_render \
    kms_setmode \
    pm_pc8 \
    gem_seqno_wrap \
    gem_render_copy \
    pm_lpsp

tests_list := $(filter-out $(skip_tests_list),$(TESTS_progs) $(TESTS_progs_M) $(HANG) $(TESTS_testsuite))

$(foreach item,$(tests_list),$(eval $(call add_test,$(item))))


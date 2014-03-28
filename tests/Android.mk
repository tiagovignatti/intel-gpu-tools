LOCAL_PATH := $(call my-dir)

include $(LOCAL_PATH)/Makefile.sources

#================#

define add_test
    include $(CLEAR_VARS)

    LOCAL_SRC_FILES := $1.c

    LOCAL_CFLAGS += -DHAVE_STRUCT_SYSINFO_TOTALRAM
    LOCAL_CFLAGS += -DANDROID -UNDEBUG -include "check-ndebug.h"
    LOCAL_CFLAGS += -std=c99
    # FIXME: drop once Bionic correctly annotates "noreturn" on pthread_exit
    LOCAL_CFLAGS += -Wno-error=return-type
    # Excessive complaining for established cases. Rely on the Linux version warnings.
    LOCAL_CFLAGS += -Wno-sign-compare

    LOCAL_C_INCLUDES = $(LOCAL_PATH)/../lib
    LOCAL_C_INCLUDES += ${ANDROID_BUILD_TOP}/external/PRIVATE/drm/include/drm

    LOCAL_MODULE := $1
    LOCAL_MODULE_TAGS := optional

    LOCAL_STATIC_LIBRARIES := libintel_gpu_tools

    LOCAL_SHARED_LIBRARIES := libpciaccess  \
                              libdrm        \
                              libdrm_intel

    include $(BUILD_EXECUTABLE)
endef

#================#

skip_tests_list := \
    kms_plane \
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


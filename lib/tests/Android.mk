LOCAL_PATH := $(call my-dir)

include $(LOCAL_PATH)/Makefile.sources

#================#
# each igt test is a separate executable. define a function to build one of these tests
define add_test
    include $(CLEAR_VARS)

    # specific to this test
    LOCAL_SRC_FILES := $1.c
    LOCAL_MODULE := $1

    # common to all tests
    LOCAL_CFLAGS += ${IGT_LOCAL_CFLAGS}
    LOCAL_C_INCLUDES = ${IGT_LOCAL_C_INCLUDES}
    LOCAL_STATIC_LIBRARIES := ${IGT_LOCAL_STATIC_LIBRARIES}
    LOCAL_SHARED_LIBRARIES := ${IGT_LOCAL_SHARED_LIBRARIES}

    LOCAL_MODULE_TAGS := optional
    LOCAL_MODULE_PATH := $(ANDROID_PRODUCT_OUT)/$(TARGET_COPY_OUT_VENDOR)/intel/validation/core/igt

    include $(BUILD_EXECUTABLE)
endef

# set local compilation flags for IGT tests
IGT_LOCAL_CFLAGS += -DHAVE_STRUCT_SYSINFO_TOTALRAM -DANDROID -UNDEBUG
IGT_LOCAL_CFLAGS += -std=gnu99
# FIXME: drop once Bionic correctly annotates "noreturn" on pthread_exit
IGT_LOCAL_CFLAGS += -Wno-error=return-type

# set local includes
IGT_LOCAL_C_INCLUDES = $(LOCAL_PATH)/../lib
IGT_LOCAL_C_INCLUDES += ${ANDROID_BUILD_TOP}/external/PRIVATE/drm/include/drm

# set local libraries
IGT_LOCAL_STATIC_LIBRARIES := libintel_gpu_tools
IGT_LOCAL_SHARED_LIBRARIES := libpciaccess libdrm libdrm_intel

$(foreach item,$(check_PROGRAMS),$(eval $(call add_test,$(item))))


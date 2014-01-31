LOCAL_PATH := $(call my-dir)

include $(LOCAL_PATH)/Makefile.sources

skip_lib_list := \
    igt_kms.c \
    igt_kms.h

lib_list := $(filter-out $(skip_lib_list),$(libintel_tools_la_SOURCES))

include $(CLEAR_VARS)

LOCAL_SRC_FILES := $(lib_list)

LOCAL_C_INCLUDES +=              \
	$(LOCAL_PATH)/..

LOCAL_EXPORT_C_INCLUDE_DIRS += $(LOCAL_PATH)

LOCAL_CFLAGS += -DHAVE_LIBDRM_ATOMIC_PRIMITIVES
LOCAL_CFLAGS += -DHAVE_STRUCT_SYSINFO_TOTALRAM
LOCAL_CFLAGS += -DANDROID
LOCAL_CFLAGS += -std=c99
LOCAL_MODULE:= libintel_gpu_tools

LOCAL_SHARED_LIBRARIES := libpciaccess  \
			  libdrm        \
			  libdrm_intel

include $(BUILD_STATIC_LIBRARY)

